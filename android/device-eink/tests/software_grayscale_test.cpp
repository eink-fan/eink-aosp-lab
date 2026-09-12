#include "eink_software_grayscale.h"
#include "eink_grayscale_delta.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using neo2::eink::ConvertToGrayscale;
using neo2::eink::MeasureGrayscaleDelta;
using neo2::eink::Rect;
using neo2::eink::RgbaLayout;
using neo2::eink::RgbaSource;
using neo2::eink::Rotation;
using neo2::eink::TransformRect;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void RequirePixels(const std::vector<std::uint8_t>& actual,
                   const std::vector<std::uint8_t>& expected, const char* message) {
  Require(actual == expected, message);
}

std::shared_ptr<neo2::eink::OwnedGrayscaleBuffer> ConvertOpaqueGray(
        const std::vector<std::uint8_t>& gray, int width, int height) {
  std::vector<std::uint8_t> rgba;
  rgba.reserve(gray.size() * 4);
  for (const auto value : gray) {
    rgba.insert(rgba.end(), {value, value, value, 255});
  }
  return ConvertToGrayscale({.pixels = rgba.data(),
                             .width = width,
                             .height = height,
                             .stride_pixels = width,
                             .layout = RgbaLayout::kRgba8888},
                            Rotation::kNone);
}

}  // namespace

int main() {
  // Source rows: [black, white], [red, green], [blue, gray].
  const std::vector<std::uint8_t> rgba = {
          0, 0, 0, 255, 255, 255, 255, 255,
          255, 0, 0, 255, 0, 255, 0, 255,
          0, 0, 255, 255, 128, 128, 128, 255,
  };
  const RgbaSource source{
          .pixels = rgba.data(), .width = 2, .height = 3, .stride_pixels = 2,
          .layout = RgbaLayout::kRgba8888};

  const auto identity = ConvertToGrayscale(source, Rotation::kNone);
  Require(identity != nullptr, "identity conversion failed");
  Require(identity->Width() == 2 && identity->Height() == 3, "identity dimensions changed");
  RequirePixels(identity->Pixels(), {0, 240, 64, 144, 16, 128},
                "panel gray encoding is wrong");

  // Exercise vector-sized rows and tails, padding, both channel orders and
  // independently varied RGB/alpha against the scalar panel formula.
  for (const int width : {1, 7, 16, 31, 64, 65}) {
    constexpr int height = 3;
    const int stride = width + 5;
    for (const auto layout : {RgbaLayout::kRgba8888, RgbaLayout::kBgra8888}) {
      std::vector<std::uint8_t> pixels(stride * height * 4, 0xa5);
      std::vector<std::uint8_t> expected;
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          const auto r = static_cast<std::uint8_t>(x * 37 + y * 71);
          const auto g = static_cast<std::uint8_t>(x * 83 + y * 19);
          const auto b = static_cast<std::uint8_t>(x * 13 + y * 103);
          auto* pixel = pixels.data() + (y * stride + x) * 4;
          pixel[layout == RgbaLayout::kRgba8888 ? 0 : 2] = r;
          pixel[1] = g;
          pixel[layout == RgbaLayout::kRgba8888 ? 2 : 0] = b;
          pixel[3] = static_cast<std::uint8_t>(x + y);
          expected.push_back(((38 * r + 75 * g + 15 * b) >> 7) & 0xf0);
        }
      }
      const auto result = ConvertToGrayscale(
              {pixels.data(), width, height, stride, layout}, Rotation::kNone);
      Require(result != nullptr, "strided conversion failed");
      RequirePixels(result->Pixels(), expected, "contiguous conversion differs from scalar formula");
    }
  }

  std::vector<std::uint8_t> all_gray_values(256);
  for (std::size_t index = 0; index < all_gray_values.size(); ++index) {
    all_gray_values[index] = static_cast<std::uint8_t>(index);
  }
  const auto panel_ramp = ConvertOpaqueGray(all_gray_values, 256, 1);
  Require(panel_ramp != nullptr, "panel gray ramp conversion failed");
  for (std::size_t index = 0; index < all_gray_values.size(); ++index) {
    Require(panel_ramp->Pixels()[index] == (all_gray_values[index] & 0xf0),
            "panel encoding did not floor a gray-domain boundary");
  }

  // A compact black/white cursor toggle remains a compact delta after the
  // panel mapping; it is not blurred or widened by quantization.
  std::vector<std::uint8_t> cursor_off(5 * 7, 240);
  std::vector<std::uint8_t> cursor_on = cursor_off;
  for (int row = 1; row < 6; ++row) cursor_on[static_cast<std::size_t>(row) * 5 + 2] = 0;
  const auto cursor_off_gray =
          ConvertOpaqueGray(cursor_off, 5, 7);
  const auto cursor_on_gray =
          ConvertOpaqueGray(cursor_on, 5, 7);
  Require(cursor_off_gray != nullptr && cursor_on_gray != nullptr, "cursor conversion failed");
  const auto cursor_delta = MeasureGrayscaleDelta(
          cursor_off_gray->Data(), cursor_on_gray->Data(), cursor_on_gray->Size(), 5, 7);
  Require(cursor_delta && cursor_delta->changed_pixels == 5 && cursor_delta->left == 2 &&
                  cursor_delta->top == 1 && cursor_delta->right == 3 && cursor_delta->bottom == 6,
          "panel encoding did not preserve the compact cursor delta");

  const auto clockwise = ConvertToGrayscale(source, Rotation::k90Clockwise);
  Require(clockwise != nullptr, "clockwise conversion failed");
  Require(clockwise->Width() == 3 && clockwise->Height() == 2, "clockwise dimensions wrong");
  // Clockwise rows: [blue, red, black], [gray, green, white].
  RequirePixels(clockwise->Pixels(), {16, 64, 0, 128, 144, 240}, "clockwise pixels wrong");

  const Rect transformed = TransformRect({.left = 0, .top = 0, .right = 1, .bottom = 2},
                                         2, 3, Rotation::k90Clockwise);
  Require(transformed.left == 1 && transformed.top == 0 && transformed.right == 3 &&
                  transformed.bottom == 1,
          "clockwise damage transform wrong");
  Require(!TransformRect({.left = -1, .top = 0, .right = 1, .bottom = 1}, 2, 3,
                         Rotation::kNone)
                   .IsValid(),
          "invalid damage rectangle was accepted");

  neo2::eink::GrayscaleFrameTracker tracker;
  std::vector<std::uint8_t> tracked(65, 0);
  auto track = [&]() {
    return tracker.Update(neo2::eink::OwnedGrayscaleBuffer(13, 5, tracked));
  };
  auto signature = track();
  Require(signature != 0 && track() == signature, "identical grayscale changed signature");
  for (std::size_t pixel = 0; pixel < tracked.size(); ++pixel) {
    tracked[pixel] = 240;
    const auto changed = track();
    Require(changed != signature && track() == changed, "missed grayscale pixel change");
    tracked[pixel] = 0;
    signature = track();
    Require(signature != changed, "return to prior content must be a new change");
  }
  const auto reshaped = tracker.Update(neo2::eink::OwnedGrayscaleBuffer(5, 13, tracked));
  Require(reshaped != signature, "same byte count geometry change suppressed");
  tracker.Reset();
  signature = track();
  Require(signature != reshaped, "reset reused preceding signature");
  Require(tracker.Update(neo2::eink::OwnedGrayscaleBuffer(13, 5, {})) == 0,
          "invalid grayscale accepted");
  Require(track() == signature, "invalid frame corrupted history");
  // Different source colors inside the same panel quantization bucket must
  // remain suppressed; overlays changing final panel bytes must be detected.
  const auto dark_a = ConvertOpaqueGray({1}, 1, 1);
  const auto dark_b = ConvertOpaqueGray({14}, 1, 1);
  const auto dark_signature = tracker.Update(*dark_a);
  Require(tracker.Update(*dark_b) == dark_signature, "same panel gray not suppressed");
  Require(tracker.Update(neo2::eink::OwnedGrayscaleBuffer(1, 1, {240})) != dark_signature,
          "final overlay change suppressed");

  std::cout << "PASS\n";
  return 0;
}
