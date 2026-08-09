#include "eink_software_grayscale.h"

#include <limits>
#include <utility>

namespace neo2::eink {

namespace {

bool CanAllocatePixels(int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  const auto max = std::numeric_limits<std::size_t>::max();
  return static_cast<std::size_t>(width) <= max / static_cast<std::size_t>(height);
}

std::uint8_t Luma(const std::uint8_t* pixel, RgbaLayout layout) {
  const int red = layout == RgbaLayout::kRgba8888 ? pixel[0] : pixel[2];
  const int green = pixel[1];
  const int blue = layout == RgbaLayout::kRgba8888 ? pixel[2] : pixel[0];
  // Matches the stock ordinary rgba_to_gray_neon kernel:
  // (38 * R + 75 * G + 15 * B) >> 7.
  return static_cast<std::uint8_t>((38 * red + 75 * green + 15 * blue) >> 7);
}

std::uint8_t PanelGray(const std::uint8_t* pixel, RgbaLayout layout) {
  // Two independent stock captures establish a 16-level panel-input domain.
  // This is the only supported Neo 2 panel encoding: a dense 8-bit luma
  // stream visibly broadens normal differential output.
  return static_cast<std::uint8_t>(Luma(pixel, layout) & 0xf0);
}

}  // namespace

OwnedGrayscaleBuffer::OwnedGrayscaleBuffer(int width, int height,
                                           std::vector<std::uint8_t> pixels)
    : width_(width), height_(height), pixels_(std::move(pixels)) {}

std::shared_ptr<OwnedGrayscaleBuffer> ConvertToGrayscale(const RgbaSource& source,
                                                          Rotation rotation) {
  if (!source.IsValid()) {
    return nullptr;
  }

  const bool rotated = rotation != Rotation::kNone;
  const int output_width = rotated ? source.height : source.width;
  const int output_height = rotated ? source.width : source.height;
  if (!CanAllocatePixels(output_width, output_height)) {
    return nullptr;
  }

  std::vector<std::uint8_t> output(static_cast<std::size_t>(output_width) * output_height);
  for (int y = 0; y < source.height; ++y) {
    for (int x = 0; x < source.width; ++x) {
      const auto source_offset =
              (static_cast<std::size_t>(y) * source.stride_pixels + x) * 4;
      int destination_x = x;
      int destination_y = y;
      if (rotation == Rotation::k90Clockwise) {
        destination_x = source.height - 1 - y;
        destination_y = x;
      } else if (rotation == Rotation::k90CounterClockwise) {
        destination_x = y;
        destination_y = source.width - 1 - x;
      }
      output[static_cast<std::size_t>(destination_y) * output_width + destination_x] =
              PanelGray(source.pixels + source_offset, source.layout);
    }
  }
  return std::make_shared<OwnedGrayscaleBuffer>(output_width, output_height, std::move(output));
}

Rect TransformRect(const Rect& source_rect, int source_width, int source_height,
                   Rotation rotation) {
  if (!source_rect.IsValid() || source_width <= 0 || source_height <= 0 ||
      source_rect.right > source_width || source_rect.bottom > source_height) {
    return {};
  }
  switch (rotation) {
    case Rotation::kNone:
      return source_rect;
    case Rotation::k90Clockwise:
      return {
              .left = source_height - source_rect.bottom,
              .top = source_rect.left,
              .right = source_height - source_rect.top,
              .bottom = source_rect.right,
      };
    case Rotation::k90CounterClockwise:
      return {
              .left = source_rect.top,
              .top = source_width - source_rect.right,
              .right = source_rect.bottom,
              .bottom = source_width - source_rect.left,
      };
  }
  return {};
}

}  // namespace neo2::eink
