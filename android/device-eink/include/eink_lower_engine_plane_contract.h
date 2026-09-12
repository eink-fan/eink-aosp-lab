#pragma once

#include "eink_device_profile.h"
#include "eink_presentation_adapter.h"

#include <cstddef>
#include <limits>
#include <vector>

namespace neo2::eink {

// Portable ownership/shape gate for the direct lower-engine call. Monochrome
// devices submit only an owned gray plane. A color-CFA device must retain the
// owned RGBA snapshot and its derived gray plane together until Submit()
// returns; the Android backend performs the stronger GraphicBuffer/type check.
// Epoch-bound retained sleep frames instead expand their owned gray plane into
// backend-owned RGBA storage; ordinary capture never receives this exception.
enum class LowerEnginePlaneStatus {
  kReady,
  kUnexpectedSourcePlane,
  kMissingGrayscalePlane,
  kInvalidGrayscaleSize,
  kMissingColorPlane,
  kUnexpectedColorPlane,
};

[[nodiscard]] inline LowerEnginePlaneStatus ValidateLowerEnginePlanes(
        const EinkDeviceProfile& profile, const Frame& frame) {
  if (frame.source_buffer != nullptr) {
    return LowerEnginePlaneStatus::kUnexpectedSourcePlane;
  }
  if (frame.grayscale_buffer == nullptr) {
    return LowerEnginePlaneStatus::kMissingGrayscalePlane;
  }
  if (profile.panel_height != 0 &&
      profile.panel_width > std::numeric_limits<std::size_t>::max() / profile.panel_height) {
    return LowerEnginePlaneStatus::kInvalidGrayscaleSize;
  }
  const std::size_t expected_gray_bytes =
          static_cast<std::size_t>(profile.panel_width) * profile.panel_height;
  if (frame.grayscale_buffer->ByteCount() != expected_gray_bytes) {
    return LowerEnginePlaneStatus::kInvalidGrayscaleSize;
  }
  if (profile.has_color_cfa) {
    // Lifecycle catalog frames own final gray bytes rather than a captured
    // GraphicBuffer. The backend expands only this epoch-bound exception.
    return frame.compose_buffer != nullptr || frame.sleep_image_epoch != 0
                                           ? LowerEnginePlaneStatus::kReady
                                           : LowerEnginePlaneStatus::kMissingColorPlane;
  }
  return frame.compose_buffer == nullptr ? LowerEnginePlaneStatus::kReady
                                         : LowerEnginePlaneStatus::kUnexpectedColorPlane;
}

[[nodiscard]] inline bool ExpandSleepGrayscaleToRgba(
        const Frame& frame, std::vector<std::uint8_t>* rgba) {
  if (frame.sleep_image_epoch == 0 || frame.compose_buffer != nullptr ||
      frame.source_buffer != nullptr || frame.grayscale_buffer == nullptr || rgba == nullptr) {
    return false;
  }
  const auto bytes = frame.grayscale_buffer->ByteCount();
  const auto* gray = frame.grayscale_buffer->Data();
  if (gray == nullptr || bytes == 0 || bytes > std::numeric_limits<std::size_t>::max() / 4) {
    return false;
  }
  rgba->resize(bytes * 4);
  for (std::size_t i = 0; i < bytes; ++i) {
    const auto value = static_cast<std::uint8_t>((gray[i] & 0xf0) | (gray[i] >> 4));
    (*rgba)[i * 4] = value;
    (*rgba)[i * 4 + 1] = value;
    (*rgba)[i * 4 + 2] = value;
    (*rgba)[i * 4 + 3] = 0xff;
  }
  return true;
}

}  // namespace neo2::eink
