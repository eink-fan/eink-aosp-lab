#pragma once

#include <cstdint>

namespace neo2::eink::android {

enum class OceanWaveformCompatibilityResult : std::uint8_t {
  kApplied,
  kInvalidEngine,
  kUnexpectedElf,
  kRelocationNotFound,
  kUnexpectedRelocationTarget,
  kProtectionChangeFailed,
};

// Replaces only Ocean libidisplayengine's strncpy PLT relocation with the
// project-owned scalar equivalent. This is installed after exact payload load
// and before iDisplayEngine_init; every mismatch fails closed.
[[nodiscard]] OceanWaveformCompatibilityResult
InstallOceanWaveformScalarStrncpyCompatibility(void* engine_handle);

}  // namespace neo2::eink::android
