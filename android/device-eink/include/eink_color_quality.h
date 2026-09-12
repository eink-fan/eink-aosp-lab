#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace neo2::eink {
struct ColorQualityThresholds {
  int chroma = 24;
  int change = 12;
  int near_white = 232;
};
// History is advanced only after the lower engine accepts the target. Preparing
// or dropping a candidate never advances it. Packed RGBA; alpha is ignored.
class ColorQualityHistory {
 public:
  std::size_t Prepare(std::span<const std::uint8_t> target,
                      std::vector<std::uint8_t>& intermediate,
                      ColorQualityThresholds thresholds = {}, bool cleanup = false) const;
  void Accept(std::span<const std::uint8_t> target);
  void Reset();
 private:
  std::vector<std::uint8_t> previous_;
};
}  // namespace neo2::eink
