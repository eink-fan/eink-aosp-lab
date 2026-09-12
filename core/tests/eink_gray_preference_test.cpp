#include "eink_gray_preference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace einklab;
static void Require(bool ok) { if (!ok) std::abort(); }

static void Check(std::array<std::uint8_t, 4> source, GrayPreference mode) {
  auto actual = source;
  ApplyGrayPreference(actual, mode);
  Require(actual[3] == source[3]);
  const int spread = std::max({source[0], source[1], source[2]}) -
          std::min({source[0], source[1], source[2]});
  if (mode == GrayPreference::kOriginal) { Require(actual == source); return; }
  const int low = mode == GrayPreference::kBalanced ? 8 : 16;
  const int high = mode == GrayPreference::kBalanced ? 24 : 40;
  // Independent floating-point reference matches the physical test-image rule.
  const double t = std::clamp(double(spread - low) / (high - low), 0.0, 1.0);
  const double weight = t * t * (3.0 - 2.0 * t);
  const int luma = (38 * source[0] + 75 * source[1] + 15 * source[2] + 64) / 128;
  for (int c = 0; c < 3; ++c) {
    const int expected = static_cast<int>(std::floor(
            luma + weight * (source[c] - luma) + 0.5 + 1e-9));
    Require(actual[c] == expected);
  }
  if (spread <= low) Require(actual[0] == actual[1] && actual[1] == actual[2]);
  if (spread >= high) Require(actual == source);
}

int main() {
  for (int r = 0; r <= 255; r += 5) for (int g = 0; g <= 255; g += 5)
      for (int b = 0; b <= 255; b += 5) {
    std::array<std::uint8_t, 4> pixel{static_cast<std::uint8_t>(r),
            static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), 73};
    const auto original = pixel;
    ApplyVividColor(pixel);
    const int peak = std::max({r, g, b}), low = std::min({r, g, b});
    Require(pixel[3] == 73);
    Require(std::max({pixel[0], pixel[1], pixel[2]}) == peak);
    for (int c = 0; c < 3; ++c) {
      Require(pixel[c] <= original[c]);
      Require(original[c] - pixel[c] <= (low + 3) / 4);
      for (int d = 0; d < 3; ++d)
        if (original[c] >= original[d]) Require(pixel[c] >= pixel[d]);
    }
    if (peak - low <= 24 || low == 0) Require(pixel == original);
    if (peak - low >= 64 && low >= 4) Require(pixel != original);
  }
  std::array<std::uint8_t, 5> malformed{200, 100, 100, 73, 9};
  const auto unchangedMalformed = malformed;
  ApplyVividColor(malformed);
  Require(malformed == unchangedMalformed);
  ApplyVividColor({});

  Require(!IsGrayPreference(-1) && IsGrayPreference(0) && IsGrayPreference(2) &&
          !IsGrayPreference(3));
  for (auto mode : {GrayPreference::kOriginal, GrayPreference::kBalanced,
                    GrayPreference::kStronger}) {
    // Exercise each threshold at every brightness, both tint directions and
    // each channel, including clipped extremes and nonopaque alpha.
    for (int base = 0; base < 256; ++base) {
      for (int delta = -42; delta <= 42; ++delta) {
        for (int channel = 0; channel < 3; ++channel) {
          std::array<std::uint8_t, 4> color{static_cast<std::uint8_t>(base),
                  static_cast<std::uint8_t>(base), static_cast<std::uint8_t>(base), 73};
          color[channel] = static_cast<std::uint8_t>(std::clamp(base + delta, 0, 255));
          Check(color, mode);
        }
      }
    }
    for (int r = 0; r < 256; r += 17)
      for (int g = 0; g < 256; g += 17)
        for (int b = 0; b < 256; b += 17)
          Check({static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                 static_cast<std::uint8_t>(b), 255}, mode);
    // A bulk call exercises ARM vector blocks and an incomplete final block.
    // Compare to independently processed single pixels (the scalar path).
    std::vector<std::uint8_t> bulk, expected;
    for (int i = 0; i < 65539; ++i) {
      std::array<std::uint8_t, 4> pixel{
              static_cast<std::uint8_t>(i & 255),
              static_cast<std::uint8_t>((i + (i / 256) % 85 - 42) & 255),
              static_cast<std::uint8_t>((i + (i / 512) % 85 - 42) & 255),
              static_cast<std::uint8_t>((i * 7) & 255)};
      bulk.insert(bulk.end(), pixel.begin(), pixel.end());
      Check(pixel, mode);
      ApplyGrayPreference(pixel, mode);
      expected.insert(expected.end(), pixel.begin(), pixel.end());
    }
    ApplyGrayPreference(bulk, mode);
    Require(bulk == expected);
  }
  std::array<std::uint8_t, 4> near_gray{126,128,130,19};
  ApplyGrayPreference(near_gray, GrayPreference::kBalanced);
  Require((near_gray == std::array<std::uint8_t,4>{128,128,128,19}));
  std::vector<std::uint8_t> packed{126,128,130,1, 200,100,20,2, 0,0,0,3};
  ApplyGrayPreference(packed, GrayPreference::kBalanced);
  Require((packed == std::vector<std::uint8_t>{128,128,128,1, 200,100,20,2, 0,0,0,3}));
  auto unchanged = packed;
  ApplyGrayPreference(packed, static_cast<GrayPreference>(255));
  Require(packed == unchanged);
  packed.push_back(7); unchanged = packed;
  ApplyGrayPreference(packed, GrayPreference::kStronger);
  Require(packed == unchanged);
  ApplyGrayPreference({}, GrayPreference::kBalanced);
  std::cout << "PASS\n";
}
