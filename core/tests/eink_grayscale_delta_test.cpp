#include "eink_grayscale_delta.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using einklab::ClassifyDeltaCoverage;
  using einklab::ClassifyDeltaDirection;
  using einklab::DeltaCoverageBucket;
  using einklab::GrayscaleDeltaDirection;
  using einklab::MeasureGrayscaleDelta;

  const std::array<std::uint8_t, 12> before{};
  auto unchanged = MeasureGrayscaleDelta(before.data(), before.data(), before.size(), 4, 3);
  Require(unchanged && unchanged->changed_pixels == 0 && unchanged->total_pixels == 12,
          "unchanged image did not retain total-pixel evidence");
  Require(unchanged->left == 0 && unchanged->top == 0 && unchanged->right == 0 &&
                  unchanged->bottom == 0,
          "unchanged image did not have an empty rectangle");
  Require(ClassifyDeltaCoverage(*unchanged) == DeltaCoverageBucket::kNoChange,
          "unchanged image did not use the zero bucket");

  auto after = before;
  after[1] = 1;
  after[10] = 2;
  const auto delta = MeasureGrayscaleDelta(before.data(), after.data(), after.size(), 4, 3);
  Require(delta && delta->changed_pixels == 2 && delta->left == 1 && delta->top == 0 &&
                  delta->right == 3 && delta->bottom == 3,
          "changed-pixel rectangle is wrong");
  Require(delta->CoveragePpm() == 166666U,
          "coverage is not computed as an integer proportion");
  Require(ClassifyDeltaCoverage(*delta) == DeltaCoverageBucket::kUnderTwentyFivePercent,
          "coverage bucket is wrong");
  Require(delta->lighter_pixels == 2 && delta->darker_pixels == 0 &&
                  ClassifyDeltaDirection(*delta) == GrayscaleDeltaDirection::kLighter,
          "lighter-only direction aggregate is wrong");

  auto darker = after;
  darker[1] = 0;
  const auto mixed = MeasureGrayscaleDelta(after.data(), darker.data(), after.size(), 4, 3);
  Require(mixed && mixed->lighter_pixels == 0 && mixed->darker_pixels == 1 &&
                  ClassifyDeltaDirection(*mixed) == GrayscaleDeltaDirection::kDarker,
          "darker-only direction aggregate is wrong");

  Require(!MeasureGrayscaleDelta(before.data(), after.data(), after.size(), 5, 3),
          "incompatible byte count was accepted");
  Require(!MeasureGrayscaleDelta(nullptr, after.data(), after.size(), 4, 3),
          "null buffer was accepted");
  std::cout << "PASS\n";
  return 0;
}
