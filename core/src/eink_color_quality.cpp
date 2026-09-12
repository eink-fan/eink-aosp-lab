#include "eink_color_quality.h"
#include <algorithm>
#include <cstdlib>
namespace einklab {
std::size_t ColorQualityHistory::Prepare(std::span<const std::uint8_t> target,
        std::vector<std::uint8_t>& intermediate, ColorQualityThresholds thresholds,
        bool cleanup) const {
  intermediate.clear();
  if (target.empty() || target.size() % 4 != 0) return 0;
  thresholds.chroma = std::clamp(thresholds.chroma, 1, 255);
  thresholds.change = std::clamp(thresholds.change, 1, 255);
  thresholds.near_white = std::clamp(thresholds.near_white, 0, 255);
  const auto byte_count = static_cast<std::size_t>(target.size());
  const bool history = previous_.size() == byte_count;
  if (!history && !cleanup) return 0;
  std::size_t count = 0;
  for (std::size_t i = 0; i < byte_count; i += 4) {
    const int hi = std::max({target[i], target[i+1], target[i+2]});
    const int lo = std::min({target[i], target[i+1], target[i+2]});
    if (hi - lo < thresholds.chroma) continue;
    if (!cleanup && history) {
      const int old_min = std::min({previous_[i], previous_[i+1], previous_[i+2]});
      if (old_min >= thresholds.near_white) continue;
      const int change = std::max({std::abs(int(target[i])-previous_[i]),
              std::abs(int(target[i+1])-previous_[i+1]),
              std::abs(int(target[i+2])-previous_[i+2])});
      if (change < thresholds.change) continue;
    }
    if (count++ == 0) intermediate.assign(target.begin(), target.end());
    std::fill_n(intermediate.begin() + i, 4, 255);
  }
  return count;
}
void ColorQualityHistory::Accept(std::span<const std::uint8_t> target) {
  if (target.empty() || target.size() % 4 != 0) { Reset(); return; }
  previous_.assign(target.begin(), target.end());
}
void ColorQualityHistory::Reset() { previous_.clear(); }
}  // namespace einklab
