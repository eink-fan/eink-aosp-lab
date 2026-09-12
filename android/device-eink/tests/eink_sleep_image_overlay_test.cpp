#include "eink_sleep_image_overlay.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using neo2::eink::ComposeSleepImageOverlay;
using neo2::eink::SleepImageOverlayResult;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  const std::vector<std::uint8_t> color{255, 0, 0, 255, 0, 0, 255, 128, 0, 255, 0, 0};
  const auto rgb = ComposeSleepImageOverlay({std::vector<std::uint8_t>{0x40, 0x10, 0x90}},
          {std::vector<std::uint8_t>{255, 128, 0}},
          {std::vector<std::uint8_t>{0xf0, 0xf0, 0xf0}}, color);
  Require(rgb.result == SleepImageOverlayResult::kComposited &&
          rgb.rgba == std::vector<std::uint8_t>({255, 0, 0, 255, 127, 127, 255, 255,
                                               255, 255, 255, 255}),
          "sleep RGB lost color or alpha/white padding");
  const auto invalid_rgb = ComposeSleepImageOverlay(
          {std::vector<std::uint8_t>{0x40}}, {std::vector<std::uint8_t>{255}},
          {std::vector<std::uint8_t>{0xf0}}, {std::vector<std::uint8_t>{1, 2, 3}});
  Require(invalid_rgb.result == SleepImageOverlayResult::kRejectedBytes,
          "truncated RGB accepted");
  const std::vector<std::uint8_t> foreground{0x10, 0xe0, 0x00, 0xf0};
  const std::vector<std::uint8_t> background{0xe0, 0x20, 0xf0, 0x00};
  const std::vector<std::uint8_t> alpha{0, 255, 128, 64};
  const auto composed = ComposeSleepImageOverlay(foreground, alpha, background);
  Require(composed.result == SleepImageOverlayResult::kComposited,
          "valid overlay was rejected");
  Require(composed.panel_gray == std::vector<std::uint8_t>({0xe0, 0xe0, 0x80, 0x40}),
          "source-over output or 16-level quantization differs");

  const auto transparent = ComposeSleepImageOverlay(
          foreground, std::vector<std::uint8_t>(foreground.size(), 0), background);
  Require(transparent.result == SleepImageOverlayResult::kComposited &&
                  transparent.panel_gray == background,
          "alpha zero did not preserve the accepted background exactly");

  const auto opaque = ComposeSleepImageOverlay(
          foreground, std::vector<std::uint8_t>(foreground.size(), 255), background);
  Require(opaque.result == SleepImageOverlayResult::kComposited &&
                  opaque.panel_gray == foreground,
          "alpha 255 did not preserve the foreground exactly");

  const auto light_background = ComposeSleepImageOverlay(
          std::vector<std::uint8_t>{0x00}, std::vector<std::uint8_t>{128},
          std::vector<std::uint8_t>{0xf0});
  const auto dark_background = ComposeSleepImageOverlay(
          std::vector<std::uint8_t>{0x00}, std::vector<std::uint8_t>{128},
          std::vector<std::uint8_t>{0x30});
  Require(light_background.panel_gray == std::vector<std::uint8_t>{0x80} &&
                  dark_background.panel_gray == std::vector<std::uint8_t>{0x20},
          "different page backgrounds did not produce distinct overlays");

  Require(ComposeSleepImageOverlay({}, {}, {}).result ==
                  SleepImageOverlayResult::kRejectedBytes,
          "empty overlay was accepted");
  Require(ComposeSleepImageOverlay({foreground.data(), 2}, {alpha.data(), 1},
                                   {background.data(), 2})
                  .result == SleepImageOverlayResult::kRejectedBytes,
          "mismatched overlay extents were accepted");
  Require(ComposeSleepImageOverlay(std::vector<std::uint8_t>{0x11},
                                   std::vector<std::uint8_t>{128},
                                   std::vector<std::uint8_t>{0xf0})
                  .result == SleepImageOverlayResult::kRejectedPanelGray,
          "non-panel foreground gray was accepted");
  Require(ComposeSleepImageOverlay(std::vector<std::uint8_t>{0x10},
                                   std::vector<std::uint8_t>{128},
                                   std::vector<std::uint8_t>{0xef})
                  .result == SleepImageOverlayResult::kRejectedPanelGray,
          "non-panel background gray was accepted");

  constexpr std::size_t kPanelBytes = 1448U * 1072U;
  const auto panel = ComposeSleepImageOverlay(
          std::vector<std::uint8_t>(kPanelBytes, 0x20),
          std::vector<std::uint8_t>(kPanelBytes, 128),
          std::vector<std::uint8_t>(kPanelBytes, 0xe0));
  Require(panel.result == SleepImageOverlayResult::kComposited &&
                  panel.panel_gray.size() == kPanelBytes && panel.panel_gray.front() == 0x80 &&
                  panel.panel_gray.back() == 0x80,
          "bounded panel-sized overlay differs");

  std::cout << "PASS\n";
  return 0;
}
