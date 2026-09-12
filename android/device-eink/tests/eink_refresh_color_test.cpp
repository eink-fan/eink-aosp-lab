#include "eink_software_grayscale.h"
#include "eink_frame_demand_gate.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <algorithm>

int main() {
  using namespace neo2::eink;
  std::array<std::uint8_t, 8> red{255, 0, 0, 255, 1, 2, 3, 4};
  std::array<std::uint8_t, 8> green{0, 130, 0, 255, 5, 6, 7, 8};
  const RgbaSource a{red.data(), 1, 1, 2, RgbaLayout::kRgba8888};
  const RgbaSource b{green.data(), 1, 1, 2, RgbaLayout::kRgba8888};
  bool found = false;
  for (int g = 0; g < 256; ++g) {
    green[1] = g;
    if (ConvertToGrayscale(a, Rotation::kNone)->Pixels() ==
        ConvertToGrayscale(b, Rotation::kNone)->Pixels()) { found = true; break; }
  }
  if (!found || FingerprintRgb(a) == FingerprintRgb(b)) return EXIT_FAILURE;
  FrameDemandGate gate;
  if (gate.Evaluate({FingerprintRgb(a), 3, 1, 1}) != DemandDecision::kSessionStart ||
      gate.Evaluate({FingerprintRgb(b), 3, 1, 1}) != DemandDecision::kPanelContentChanged)
    return EXIT_FAILURE;
  const auto old = FingerprintRgb(a);
  red[3] = 0; red[4] = 99; red[5] = 88;
  if (FingerprintRgb(a) != old) return EXIT_FAILURE;
  std::array<std::uint8_t, 4> bgra{0, 0, 255, 255};
  if (FingerprintRgb({bgra.data(), 1, 1, 1, RgbaLayout::kBgra8888}) != old)
    return EXIT_FAILURE;
  // Exact tracking must preserve the same-luma color transition too.
  RgbFrameTracker tracker;
  const auto first = tracker.Update(a);
  if (first == 0 || tracker.Update(a) != first || tracker.Update(b) == first)
    return EXIT_FAILURE;
  tracker.Reset();
  if (tracker.Update(b) == 0) return EXIT_FAILURE;

  // Cover vector blocks/tails, all RGB channels at every position, padding,
  // alpha-only edits, and a switch of channel layout without a content change.
  for (int width : {1, 7, 16, 31, 64, 65}) {
    constexpr int height = 3;
    const int stride = width + 3;
    std::vector<std::uint8_t> pixels(stride * height * 4, 0);
    RgbaSource source{pixels.data(), width, height, stride, RgbaLayout::kRgba8888};
    RgbFrameTracker exact;
    auto signature = exact.Update(source);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        for (int channel = 0; channel < 3; ++channel) {
          pixels[(y * stride + x) * 4 + channel] ^= static_cast<std::uint8_t>(0x81 + channel * 13);
          const auto changed = exact.Update(source);
          if (changed == signature || exact.Update(source) != changed) return EXIT_FAILURE;
          signature = changed;
        }
        pixels[(y * stride + x) * 4 + 3] = 255;
      }
      std::fill(pixels.begin() + (y * stride + width) * 4,
                pixels.begin() + (y + 1) * stride * 4, 0xab);
    }
    if (exact.Update(source) != signature) return EXIT_FAILURE;
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        std::swap(pixels[(y * stride + x) * 4], pixels[(y * stride + x) * 4 + 2]);
    source.layout = RgbaLayout::kBgra8888;
    if (exact.Update(source) != signature) return EXIT_FAILURE;
    exact.Reset();
    if (exact.Update(source) == signature) return EXIT_FAILURE;
    signature = exact.Update(source);
    if (exact.Update({}) != 0 || exact.Update(source) != signature) return EXIT_FAILURE;
  }
  // Same pixel count, different dimensions is a changed frame even if black.
  std::array<std::uint8_t, 24> black{};
  RgbFrameTracker geometry;
  const auto shape = geometry.Update({black.data(), 2, 3, 2, RgbaLayout::kRgba8888});
  if (geometry.Update({black.data(), 3, 2, 3, RgbaLayout::kRgba8888}) == shape)
    return EXIT_FAILURE;
  std::cout << "PASS\n";
}
