#pragma once

#include <cstddef>

namespace neo2::eink {

// A strncpy-compatible copy whose source is read one byte at a time. Ocean's
// controller-backed waveform mapping rejects the wide SIMD reads used by
// Android 17 bionic even though scalar reads are valid.
char* ScalarStrncpy(char* destination, const char* source, std::size_t count) noexcept;

}  // namespace neo2::eink
