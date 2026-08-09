#include "eink_grayscale_delta.h"

#include <algorithm>
#include <limits>

namespace einklab {

std::uint32_t GrayscaleDelta::CoveragePpm() const {
  if (total_pixels == 0) return 0;
  const auto ppm = (changed_pixels * 1'000'000ULL) / total_pixels;
  return static_cast<std::uint32_t>(
          std::min<std::uint64_t>(ppm, static_cast<std::uint64_t>(1'000'000U)));
}

GrayscaleDeltaDirection ClassifyDeltaDirection(const GrayscaleDelta& delta) {
  if (delta.changed_pixels == 0 || delta.lighter_pixels + delta.darker_pixels !=
                  delta.changed_pixels) {
    return GrayscaleDeltaDirection::kUnavailable;
  }
  if (delta.lighter_pixels == delta.changed_pixels) {
    return GrayscaleDeltaDirection::kLighter;
  }
  if (delta.darker_pixels == delta.changed_pixels) {
    return GrayscaleDeltaDirection::kDarker;
  }
  return GrayscaleDeltaDirection::kMixed;
}

std::optional<GrayscaleDelta> MeasureGrayscaleDelta(const std::uint8_t* previous,
                                                    const std::uint8_t* current,
                                                    std::size_t bytes, int width, int height) {
  if (previous == nullptr || current == nullptr || width <= 0 || height <= 0) return std::nullopt;
  const auto width_size = static_cast<std::size_t>(width);
  const auto height_size = static_cast<std::size_t>(height);
  if (width_size > std::numeric_limits<std::size_t>::max() / height_size ||
      width_size * height_size != bytes) {
    return std::nullopt;
  }

  GrayscaleDelta delta{.total_pixels = bytes, .left = width, .top = height};
  for (std::size_t index = 0; index < bytes; ++index) {
    if (previous[index] == current[index]) continue;
    ++delta.changed_pixels;
    if (current[index] > previous[index]) {
      ++delta.lighter_pixels;
    } else {
      ++delta.darker_pixels;
    }
    const int x = static_cast<int>(index % width_size);
    const int y = static_cast<int>(index / width_size);
    delta.left = std::min(delta.left, x);
    delta.top = std::min(delta.top, y);
    delta.right = std::max(delta.right, x + 1);
    delta.bottom = std::max(delta.bottom, y + 1);
  }
  if (delta.changed_pixels == 0) {
    delta.left = 0;
    delta.top = 0;
    delta.right = 0;
    delta.bottom = 0;
  }
  return delta;
}

DeltaCoverageBucket ClassifyDeltaCoverage(const GrayscaleDelta& delta) {
  if (delta.changed_pixels == 0) return DeltaCoverageBucket::kNoChange;
  const auto coverage_ppm = delta.CoveragePpm();
  if (coverage_ppm < 10'000U) return DeltaCoverageBucket::kUnderOnePercent;
  if (coverage_ppm < 50'000U) return DeltaCoverageBucket::kUnderFivePercent;
  if (coverage_ppm < 250'000U) return DeltaCoverageBucket::kUnderTwentyFivePercent;
  return DeltaCoverageBucket::kTwentyFivePercentOrMore;
}

}  // namespace einklab
