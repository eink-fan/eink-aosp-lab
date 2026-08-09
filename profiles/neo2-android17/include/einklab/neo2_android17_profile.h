#pragma once

#include <cstdint>

namespace einklab::neo2_android17 {

// Observed refresh-mode values for the Neo 2 Android-17 binding. The Android
// framework integration was rebased for API 37, but the locally supplied
// lower e-ink runtime uses this same mode envelope. These are not portable
// Android or panel API constants.
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

inline constexpr std::uint32_t kFullGc16Hint =
    ToPresentationHint(RefreshMode::kGc16Partial) +
    ToPresentationHint(RefreshMode::kFullUpdate);
inline constexpr std::uint32_t kNormalDifferentialHint =
    ToPresentationHint(RefreshMode::kGlr16Partial);

inline constexpr int kPanelWidth = 1448;
inline constexpr int kPanelHeight = 1072;

}  // namespace einklab::neo2_android17
