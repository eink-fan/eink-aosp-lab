#pragma once

#include <cstdint>
#include <span>

namespace einklab {

enum class GrayPreference : std::uint8_t { kOriginal = 0, kBalanced = 1, kStronger = 2 };
constexpr bool IsGrayPreference(int value) { return value >= 0 && value <= 2; }

// Diagnostic color treatment; neutral pixels and alpha are unchanged.
void ApplyVividColor(std::span<std::uint8_t> rgba);

// In-place, packed RGBA. Preserves alpha and approximate code-space luma.
// Balanced uses 8/24 thresholds; Stronger uses 16/40 (policy, not calibration).
// This is color treatment, independent of waveform and cleanup selection.
void ApplyGrayPreference(std::span<std::uint8_t> rgba, GrayPreference preference);

}  // namespace einklab
