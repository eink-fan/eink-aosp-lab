#pragma once

#include "eink_presentation_adapter.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace einklab {

enum class RgbaLayout {
  kRgba8888,
  kBgra8888,
};

enum class Rotation {
  kNone,
  k90Clockwise,
  k90CounterClockwise,
};

struct RgbaSource {
  const std::uint8_t* pixels = nullptr;
  int width = 0;
  int height = 0;
  int stride_pixels = 0;
  RgbaLayout layout = RgbaLayout::kRgba8888;

  [[nodiscard]] bool IsValid() const {
    return pixels != nullptr && width > 0 && height > 0 && stride_pixels >= width;
  }
};

class OwnedGrayscaleBuffer final : public GrayscaleBuffer {
 public:
  OwnedGrayscaleBuffer(int width, int height, std::vector<std::uint8_t> pixels);

  [[nodiscard]] const std::uint8_t* Data() const override { return pixels_.data(); }
  [[nodiscard]] std::size_t ByteCount() const override { return pixels_.size(); }
  [[nodiscard]] int Width() const { return width_; }
  [[nodiscard]] int Height() const { return height_; }
  [[nodiscard]] std::size_t Size() const { return pixels_.size(); }
  [[nodiscard]] const std::vector<std::uint8_t>& Pixels() const { return pixels_; }

 private:
  int width_;
  int height_;
  std::vector<std::uint8_t> pixels_;
};

// Software fallback for a completed RGBA/BGRA source buffer. The transform is
// explicit: callers must not infer rotation from UI dimensions.
// Returns nullptr for an invalid source or an allocation-size overflow.
[[nodiscard]] std::shared_ptr<OwnedGrayscaleBuffer> ConvertToGrayscale(
        const RgbaSource& source, Rotation rotation);

// Transform a source-space damage rectangle into the converted buffer's
// coordinate system. The source bounds are required to transform rotations.
[[nodiscard]] Rect TransformRect(const Rect& source_rect, int source_width,
                                 int source_height, Rotation rotation);

}  // namespace einklab
