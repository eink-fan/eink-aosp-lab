#pragma once

#include <cstdint>

namespace einklab::neo2_android14 {

// Observed refresh-mode values for the Neo 2's stock-compatible Android 14
// e-ink engine family. These values are configuration hints supplied to a
// locally built same-version backend; they are not an API for another device.
enum class RefreshMode : std::uint32_t {
  kAutomatic = 0,
  kDuPartial = 1,
  kGc16Partial = 2,
  kGl16Partial = 3,
  kGlr16Partial = 4,
  kAnimationPartial = 6,
  kAutoPartial = 15,
  kFullUpdate = 32,
};

[[nodiscard]] constexpr std::uint32_t ToPresentationHint(RefreshMode mode) {
  return static_cast<std::uint32_t>(mode);
}

// The safe starting envelopes observed for a full-panel input. The backend
// must reject unknown hints rather than treating this as a general mode table.
inline constexpr std::uint32_t kFullGc16Hint =
    ToPresentationHint(RefreshMode::kGc16Partial) +
    ToPresentationHint(RefreshMode::kFullUpdate);
inline constexpr std::uint32_t kNormalDifferentialHint =
    ToPresentationHint(RefreshMode::kGlr16Partial);

inline constexpr int kPanelWidth = 1448;
inline constexpr int kPanelHeight = 1072;

}  // namespace einklab::neo2_android14
