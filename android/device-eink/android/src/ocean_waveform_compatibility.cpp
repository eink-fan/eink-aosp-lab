#include "neo2/eink/ocean_waveform_compatibility.h"

#include <eink_scalar_strncpy.h>

#include <dlfcn.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace neo2::eink::android {

namespace {

constexpr char kEngineInitializer[] = "iDisplayEngine_init";
constexpr char kTargetSymbol[] = "strncpy";
constexpr std::size_t kMaximumLoadSegments = 8;

struct MemoryRange {
  std::uintptr_t begin = 0;
  std::uintptr_t end = 0;
};

bool Contains(const MemoryRange& range, std::uintptr_t address, std::size_t size) {
  return address >= range.begin && address <= range.end && size <= range.end - address;
}

bool IsLoadedRange(const std::array<MemoryRange, kMaximumLoadSegments>& ranges,
                   std::size_t range_count, std::uintptr_t address, std::size_t size) {
  for (std::size_t index = 0; index < range_count; ++index) {
    if (Contains(ranges[index], address, size)) return true;
  }
  return false;
}

}  // namespace

OceanWaveformCompatibilityResult InstallOceanWaveformScalarStrncpyCompatibility(
        void* engine_handle) {
#if !defined(__aarch64__)
  (void)engine_handle;
  return OceanWaveformCompatibilityResult::kUnexpectedElf;
#else
  if (engine_handle == nullptr) return OceanWaveformCompatibilityResult::kInvalidEngine;
  void* const initializer = dlsym(engine_handle, kEngineInitializer);
  Dl_info info{};
  if (initializer == nullptr || dladdr(initializer, &info) == 0 || info.dli_fbase == nullptr) {
    return OceanWaveformCompatibilityResult::kInvalidEngine;
  }

  const auto base = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
  const auto* header = reinterpret_cast<const Elf64_Ehdr*>(base);
  if (std::memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 ||
      header->e_ident[EI_CLASS] != ELFCLASS64 || header->e_ident[EI_DATA] != ELFDATA2LSB ||
      header->e_machine != EM_AARCH64 || header->e_type != ET_DYN ||
      header->e_phentsize != sizeof(Elf64_Phdr) || header->e_phnum == 0) {
    return OceanWaveformCompatibilityResult::kUnexpectedElf;
  }

  const auto* program_headers = reinterpret_cast<const Elf64_Phdr*>(base + header->e_phoff);
  std::array<MemoryRange, kMaximumLoadSegments> load_ranges{};
  std::size_t load_range_count = 0;
  const Elf64_Dyn* dynamic = nullptr;
  std::size_t dynamic_count = 0;
  MemoryRange relro{};
  for (std::size_t index = 0; index < header->e_phnum; ++index) {
    const Elf64_Phdr& program = program_headers[index];
    if (program.p_vaddr > std::numeric_limits<std::uintptr_t>::max() - base ||
        program.p_memsz > std::numeric_limits<std::uintptr_t>::max() -
                                   (base + program.p_vaddr)) {
      return OceanWaveformCompatibilityResult::kUnexpectedElf;
    }
    const MemoryRange range{.begin = base + program.p_vaddr,
                            .end = base + program.p_vaddr + program.p_memsz};
    if (program.p_type == PT_LOAD) {
      if (load_range_count == load_ranges.size()) {
        return OceanWaveformCompatibilityResult::kUnexpectedElf;
      }
      load_ranges[load_range_count++] = range;
    } else if (program.p_type == PT_DYNAMIC) {
      dynamic = reinterpret_cast<const Elf64_Dyn*>(range.begin);
      dynamic_count = program.p_memsz / sizeof(Elf64_Dyn);
    } else if (program.p_type == PT_GNU_RELRO) {
      relro = range;
    }
  }
  if (dynamic == nullptr || dynamic_count == 0 || relro.begin == 0) {
    return OceanWaveformCompatibilityResult::kUnexpectedElf;
  }

  std::uintptr_t string_table = 0;
  std::size_t string_table_size = 0;
  std::uintptr_t symbol_table = 0;
  std::uintptr_t jump_relocations = 0;
  std::size_t jump_relocations_size = 0;
  std::size_t relocation_entry_size = 0;
  Elf64_Xword relocation_kind = 0;
  for (std::size_t index = 0; index < dynamic_count && dynamic[index].d_tag != DT_NULL; ++index) {
    const Elf64_Dyn& entry = dynamic[index];
    switch (entry.d_tag) {
      case DT_STRTAB:
        string_table = base + entry.d_un.d_ptr;
        break;
      case DT_STRSZ:
        string_table_size = entry.d_un.d_val;
        break;
      case DT_SYMTAB:
        symbol_table = base + entry.d_un.d_ptr;
        break;
      case DT_JMPREL:
        jump_relocations = base + entry.d_un.d_ptr;
        break;
      case DT_PLTRELSZ:
        jump_relocations_size = entry.d_un.d_val;
        break;
      case DT_RELAENT:
        relocation_entry_size = entry.d_un.d_val;
        break;
      case DT_PLTREL:
        relocation_kind = entry.d_un.d_val;
        break;
    }
  }
  if (string_table == 0 || string_table_size == 0 || symbol_table == 0 ||
      jump_relocations == 0 || jump_relocations_size == 0 ||
      relocation_entry_size != sizeof(Elf64_Rela) || relocation_kind != DT_RELA ||
      jump_relocations_size % sizeof(Elf64_Rela) != 0 ||
      !IsLoadedRange(load_ranges, load_range_count, string_table, string_table_size) ||
      !IsLoadedRange(load_ranges, load_range_count, jump_relocations, jump_relocations_size)) {
    return OceanWaveformCompatibilityResult::kUnexpectedElf;
  }

  const auto* relocations = reinterpret_cast<const Elf64_Rela*>(jump_relocations);
  void** target_slot = nullptr;
  const std::size_t relocation_count = jump_relocations_size / sizeof(Elf64_Rela);
  for (std::size_t index = 0; index < relocation_count; ++index) {
    const Elf64_Rela& relocation = relocations[index];
    const std::size_t symbol_index = ELF64_R_SYM(relocation.r_info);
    if (symbol_index >
        (std::numeric_limits<std::uintptr_t>::max() - symbol_table) / sizeof(Elf64_Sym)) {
      return OceanWaveformCompatibilityResult::kUnexpectedElf;
    }
    const std::uintptr_t symbol_address = symbol_table + symbol_index * sizeof(Elf64_Sym);
    if (!IsLoadedRange(load_ranges, load_range_count, symbol_address, sizeof(Elf64_Sym))) {
      return OceanWaveformCompatibilityResult::kUnexpectedElf;
    }
    const auto* symbol = reinterpret_cast<const Elf64_Sym*>(symbol_address);
    if (symbol->st_name >= string_table_size) {
      return OceanWaveformCompatibilityResult::kUnexpectedElf;
    }
    const char* const name = reinterpret_cast<const char*>(string_table + symbol->st_name);
    const std::size_t remaining = string_table_size - symbol->st_name;
    if (std::memchr(name, '\0', remaining) == nullptr) {
      return OceanWaveformCompatibilityResult::kUnexpectedElf;
    }
    if (std::strcmp(name, kTargetSymbol) != 0) continue;
    if (ELF64_R_TYPE(relocation.r_info) != R_AARCH64_JUMP_SLOT || target_slot != nullptr ||
        relocation.r_offset > std::numeric_limits<std::uintptr_t>::max() - base) {
      return OceanWaveformCompatibilityResult::kUnexpectedElf;
    }
    target_slot = reinterpret_cast<void**>(base + relocation.r_offset);
  }
  if (target_slot == nullptr) {
    return OceanWaveformCompatibilityResult::kRelocationNotFound;
  }
  const auto target_address = reinterpret_cast<std::uintptr_t>(target_slot);
  if (!Contains(relro, target_address, sizeof(*target_slot))) {
    return OceanWaveformCompatibilityResult::kUnexpectedElf;
  }

  void* expected = dlsym(RTLD_DEFAULT, kTargetSymbol);
  void* current = nullptr;
  std::memcpy(&current, target_slot, sizeof(current));
  if (expected == nullptr || current != expected) {
    return OceanWaveformCompatibilityResult::kUnexpectedRelocationTarget;
  }

  const long page_size_result = sysconf(_SC_PAGESIZE);
  if (page_size_result <= 0) {
    return OceanWaveformCompatibilityResult::kProtectionChangeFailed;
  }
  const auto page_size = static_cast<std::uintptr_t>(page_size_result);
  if ((page_size & (page_size - 1)) != 0) {
    return OceanWaveformCompatibilityResult::kProtectionChangeFailed;
  }
  void* const page = reinterpret_cast<void*>(target_address & ~(page_size - 1));
  if (mprotect(page, page_size, PROT_READ | PROT_WRITE) != 0) {
    return OceanWaveformCompatibilityResult::kProtectionChangeFailed;
  }
  void* replacement = reinterpret_cast<void*>(&neo2::eink::ScalarStrncpy);
  std::memcpy(target_slot, &replacement, sizeof(replacement));
  if (mprotect(page, page_size, PROT_READ) != 0) {
    return OceanWaveformCompatibilityResult::kProtectionChangeFailed;
  }
  return OceanWaveformCompatibilityResult::kApplied;
#endif
}

}  // namespace neo2::eink::android
