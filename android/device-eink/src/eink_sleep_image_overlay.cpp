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
        std::span<const std::uint8_t> background_panel_gray,
        std::span<const std::uint8_t> foreground_rgba) {
  if (foreground_panel_gray.empty() ||
      foreground_panel_gray.size() != foreground_alpha.size() ||
      foreground_panel_gray.size() != background_panel_gray.size() ||
      (!foreground_rgba.empty() && (foreground_rgba.size() / 4 != foreground_panel_gray.size() ||
                                   foreground_rgba.size() % 4 != 0))) {
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
  if (!foreground_rgba.empty()) output.rgba.resize(foreground_rgba.size());
  for (decltype(foreground_panel_gray.size()) index = 0;
       index < foreground_panel_gray.size(); ++index) {
    const std::uint32_t alpha = foreground_alpha[index];
    const std::uint32_t inverse_alpha = 255 - alpha;
    const std::uint32_t blended =
            (static_cast<std::uint32_t>(foreground_panel_gray[index]) * alpha +
             static_cast<std::uint32_t>(background_panel_gray[index]) * inverse_alpha + 127) /
            255;
    output.panel_gray[index] = QuantizePanelGray(blended);
    if (!foreground_rgba.empty()) {
      const std::uint32_t background = background_panel_gray[index] |
              (background_panel_gray[index] >> 4);
      for (std::size_t channel = 0; channel < 3; ++channel) {
        output.rgba[4 * index + channel] = static_cast<std::uint8_t>(
                (foreground_rgba[4 * index + channel] * alpha +
                 background * inverse_alpha + 127) / 255);
      }
      output.rgba[4 * index + 3] = 255;
    }
  }
  return output;
}

}  // namespace neo2::eink
