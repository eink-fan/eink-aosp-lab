#include "eink_sleep_image_overlay.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace neo2::eink {

namespace {

bool IsPanelGray(std::span<const std::uint8_t> pixels) {
  return std::all_of(pixels.begin(), pixels.end(),
                     [](std::uint8_t pixel) { return (pixel & 0x0fU) == 0; });
}

std::uint8_t QuantizePanelGray(std::uint32_t gray) {
  const std::uint32_t level = std::min<std::uint32_t>(15, (gray + 8) / 16);
  return static_cast<std::uint8_t>(level * 16);
}

}  // namespace

SleepImageOverlayOutput ComposeSleepImageOverlay(
        std::span<const std::uint8_t> foreground_panel_gray,
        std::span<const std::uint8_t> foreground_alpha,
        std::span<const std::uint8_t> background_panel_gray) {
  if (foreground_panel_gray.empty() ||
      foreground_panel_gray.size() != foreground_alpha.size() ||
      foreground_panel_gray.size() != background_panel_gray.size()) {
    return {
            .result = SleepImageOverlayResult::kRejectedBytes,
            .panel_gray = {},
    };
  }
  if (!IsPanelGray(foreground_panel_gray) || !IsPanelGray(background_panel_gray)) {
    return {
            .result = SleepImageOverlayResult::kRejectedPanelGray,
            .panel_gray = {},
    };
  }

  SleepImageOverlayOutput output{
          .result = SleepImageOverlayResult::kComposited,
          .panel_gray = std::vector<std::uint8_t>(foreground_panel_gray.size()),
  };
  for (std::size_t index = 0; index < foreground_panel_gray.size(); ++index) {
    const std::uint32_t alpha = foreground_alpha[index];
    const std::uint32_t inverse_alpha = 255 - alpha;
    const std::uint32_t blended =
            (static_cast<std::uint32_t>(foreground_panel_gray[index]) * alpha +
             static_cast<std::uint32_t>(background_panel_gray[index]) * inverse_alpha + 127) /
            255;
    output.panel_gray[index] = QuantizePanelGray(blended);
  }
  return output;
}

}  // namespace neo2::eink
