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

  std::cout << "PASS\n";
  return 0;
}
