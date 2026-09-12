#include "eink_scalar_strncpy.h"

namespace neo2::eink {

char* ScalarStrncpy(char* destination, const char* source, std::size_t count) noexcept {
  auto* volatile_source = reinterpret_cast<const volatile unsigned char*>(source);
  std::size_t index = 0;
  for (; index < count; ++index) {
    const unsigned char value = volatile_source[index];
    destination[index] = static_cast<char>(value);
    if (value == 0) {
      ++index;
      break;
    }
  }
  for (; index < count; ++index) {
    destination[index] = '\0';
  }
  return destination;
}

}  // namespace neo2::eink
