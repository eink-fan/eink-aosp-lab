#pragma once

#include "eink_software_grayscale.h"

namespace einklab {

// Exact RGB comparison of resolved composition, ignoring alpha and row padding.
// Single-worker owned. The signature identifies consecutive equal frames, not
// content across trackers. This is capture history, not accepted-target history.
class RgbFrameTracker {
 public:
  explicit RgbFrameTracker(std::size_t max_pixels) : max_pixels_(max_pixels) {}
  // available_bytes bounds readable source storage, including row strides.
  // Invalid input returns zero without changing history. The caller owns and
  // synchronizes source memory for this call; no borrowed pointer is retained.
  [[nodiscard]] std::uint64_t Update(const RgbaSource& source,
                                     std::size_t available_bytes);
  // Invalidates geometry/content, retaining the generation counter.
  void Reset();

 private:
  std::size_t max_pixels_;
  std::vector<std::uint32_t> previous_;
  int width_ = 0;
  int height_ = 0;
  std::uint64_t generation_ = 0;
};

}  // namespace einklab
