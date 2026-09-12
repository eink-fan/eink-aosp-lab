#include "eink_rgb_frame_tracker.h"

#include <limits>

namespace einklab {

void RgbFrameTracker::Reset() {
  previous_.clear();
  width_ = height_ = 0;
  // Preserve the generation so a reset cannot reuse the preceding signature.
}

std::uint64_t RgbFrameTracker::Update(const RgbaSource& source, std::size_t available_bytes) {
  if (!source.IsValid() ||
      (source.layout != RgbaLayout::kRgba8888 && source.layout != RgbaLayout::kBgra8888))
    return 0;
  const auto width = static_cast<std::size_t>(source.width);
  const auto height = static_cast<std::size_t>(source.height);
  if (width > max_pixels_ / height || width > previous_.max_size() / height ||
      static_cast<std::size_t>(source.stride_pixels) >
              std::numeric_limits<std::size_t>::max() / height / 4) return 0;
  const auto row_bytes = static_cast<std::size_t>(source.stride_pixels) * 4;
  const auto required_bytes = (height - 1) * row_bytes + width * 4;
  if (required_bytes > available_bytes) return 0;
  const bool geometry_changed = width_ != source.width || height_ != source.height;
  previous_.resize(width * height);
  // A reduction plus contiguous loads/stores lets the compiler vectorize the
  // comparison. Every channel of every pixel contributes, including row tails.
  const auto compare_rows = [&]<RgbaLayout layout>() {
    std::uint32_t difference = 0;
    for (int y = 0; y < source.height; ++y) {
      const auto* row = source.pixels + static_cast<std::size_t>(y) * source.stride_pixels * 4;
      auto* previous = previous_.data() + static_cast<std::size_t>(y) * width;
      for (int x = 0; x < source.width; ++x) {
        const auto* pixel = row + static_cast<std::size_t>(x) * 4;
        constexpr int red = layout == RgbaLayout::kRgba8888 ? 0 : 2;
        constexpr int blue = layout == RgbaLayout::kRgba8888 ? 2 : 0;
        const std::uint32_t rgb = static_cast<std::uint32_t>(pixel[red]) |
                (static_cast<std::uint32_t>(pixel[1]) << 8) |
                (static_cast<std::uint32_t>(pixel[blue]) << 16);
        difference |= previous[x] ^ rgb;
        previous[x] = rgb;
      }
    }
    return difference;
  };
  const auto difference = source.layout == RgbaLayout::kRgba8888
          ? compare_rows.template operator()<RgbaLayout::kRgba8888>()
          : compare_rows.template operator()<RgbaLayout::kBgra8888>();
  width_ = source.width;
  height_ = source.height;
  if (geometry_changed || difference != 0) {
    if (++generation_ == 0) ++generation_;
  }
  return generation_;
}

}  // namespace einklab
