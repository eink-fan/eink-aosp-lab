#include "eink_debug_property.h"

#include <charconv>

namespace neo2::eink {

BoundedDebugInt ParseBoundedDebugInt(std::string_view raw, int default_value, int minimum,
                                     int maximum) {
  if (minimum > maximum || default_value < minimum || default_value > maximum) {
    return {.value = default_value, .used_default = true, .invalid = true};
  }
  if (raw.empty()) return {.value = default_value, .used_default = true};
  int value = 0;
  const auto [cursor, error] = std::from_chars(raw.data(), raw.data() + raw.size(), value);
  if (error != std::errc{} || cursor != raw.data() + raw.size() || value < minimum ||
      value > maximum) {
    return {.value = default_value, .used_default = true, .invalid = true};
  }
  return {.value = value};
}

}  // namespace neo2::eink
