#pragma once

#include "eink_presentation_adapter.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace neo2::eink {

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

[[nodiscard]] std::uint64_t FingerprintRgb(const RgbaSource& source);

// Worker-confined exact RGB change detection. The returned signature identifies
// consecutive equal frames, not content across resets or different trackers.
// Owns packed RGB pixels; never retains a mapped source or snapshot-pool lease.
class RgbFrameTracker {
 public:
  [[nodiscard]] std::uint64_t Update(const RgbaSource& source);
  void Reset();

 private:
  std::vector<std::uint32_t> previous_;
  int width_ = 0;
  int height_ = 0;
  std::uint64_t generation_ = 0;
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

// Worker-confined exact comparison of selected panel grayscale bytes.
// Signatures identify consecutive equal frames; Reset preserves generation.
class GrayscaleFrameTracker {
 public:
  [[nodiscard]] std::uint64_t Update(const OwnedGrayscaleBuffer& frame);
  void Reset();

 private:
  std::vector<std::uint8_t> previous_;
  int width_ = 0;
  int height_ = 0;
  std::uint64_t generation_ = 0;
};

// Software fallback for a completed RGBA/BGRA source buffer. The transform is
// explicit for portability. On the measured Neo 2 stock RenderSurface target,
// the source is already the 1448x1072 panel-native buffer and uses kNone;
// callers must not infer a second rotation merely from Android UI dimensions.
// Returns nullptr for an invalid source or an allocation-size overflow.
[[nodiscard]] std::shared_ptr<OwnedGrayscaleBuffer> ConvertToGrayscale(
        const RgbaSource& source, Rotation rotation);

// Transform an Android-space damage rectangle into the converted buffer's
// coordinate system. The source bounds are required to transform rotations.
[[nodiscard]] Rect TransformRect(const Rect& source_rect, int source_width,
                                 int source_height, Rotation rotation);

}  // namespace neo2::eink
