#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace einklab {

// Difference summary for two equally sized 8-bit grayscale panel images.
// `right` and `bottom` are exclusive. The result contains counts and geometry
// only; it never retains pixel data.
struct GrayscaleDelta {
  std::uint64_t changed_pixels = 0;
  // Aggregate direction only. These counts distinguish a compact visual
  // toggle from a same-direction change without retaining source pixels.
  std::uint64_t lighter_pixels = 0;
  std::uint64_t darker_pixels = 0;
  std::uint64_t total_pixels = 0;
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  [[nodiscard]] std::uint32_t CoveragePpm() const;
};

enum class GrayscaleDeltaDirection : std::uint8_t {
  kUnavailable,
  kLighter,
  kDarker,
  kMixed,
};

[[nodiscard]] GrayscaleDeltaDirection ClassifyDeltaDirection(const GrayscaleDelta& delta);

enum class DeltaCoverageBucket : std::uint8_t {
  kNoChange,
  kUnderOnePercent,
  kUnderFivePercent,
  kUnderTwentyFivePercent,
  kTwentyFivePercentOrMore,
};

// Returns nullopt when the byte count cannot describe the supplied geometry.
// Callers own both buffers and may discard them immediately after this call.
[[nodiscard]] std::optional<GrayscaleDelta> MeasureGrayscaleDelta(
        const std::uint8_t* previous, const std::uint8_t* current, std::size_t bytes,
        int width, int height);
[[nodiscard]] DeltaCoverageBucket ClassifyDeltaCoverage(const GrayscaleDelta& delta);

}  // namespace einklab
