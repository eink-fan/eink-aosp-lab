#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace neo2::eink {

enum class SleepImageOverlayResult : std::uint8_t {
  kComposited,
  kRejectedBytes,
  kRejectedPanelGray,
};

struct SleepImageOverlayOutput {
  SleepImageOverlayResult result = SleepImageOverlayResult::kRejectedBytes;
  std::vector<std::uint8_t> panel_gray;
};

// Composites a preconverted foreground over the latest accepted ordinary
// panel frame. Foreground and background must already use the 16-level panel
// domain (low nibble zero); alpha remains the full [0, 255] coverage domain.
// The source-over result is rounded once, then deterministically quantized to
// the nearest representable panel level.
[[nodiscard]] SleepImageOverlayOutput ComposeSleepImageOverlay(
        std::span<const std::uint8_t> foreground_panel_gray,
        std::span<const std::uint8_t> foreground_alpha,
        std::span<const std::uint8_t> background_panel_gray);

}  // namespace neo2::eink
