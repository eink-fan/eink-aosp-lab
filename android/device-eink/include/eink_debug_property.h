#pragma once

#include <string_view>

namespace neo2::eink {

// Parses one volatile integer property without silently clamping a malformed
// value. The caller can count and redact an invalid raw value while still
// receiving the contract default for fail-safe operation.
struct BoundedDebugInt {
  int value = 0;
  bool used_default = false;
  bool invalid = false;
};

[[nodiscard]] BoundedDebugInt ParseBoundedDebugInt(std::string_view raw, int default_value,
                                                    int minimum, int maximum);

}  // namespace neo2::eink
