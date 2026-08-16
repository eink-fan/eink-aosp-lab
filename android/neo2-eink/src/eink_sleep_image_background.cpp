#include "eink_sleep_image_background.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace neo2::eink {

namespace {

bool ExpectedBytes(int width, int height, std::size_t* bytes) {
  if (bytes == nullptr || width <= 0 || height <= 0) return false;
  const auto checked_width = static_cast<std::size_t>(width);
  const auto checked_height = static_cast<std::size_t>(height);
  if (checked_width > std::numeric_limits<std::size_t>::max() / checked_height) return false;
  *bytes = checked_width * checked_height;
  return true;
}

bool IsPanelGray(const std::uint8_t* pixels, std::size_t size) {
  return pixels != nullptr && std::all_of(
          pixels, pixels + size, [](std::uint8_t pixel) { return (pixel & 0x0fU) == 0; });
}

}  // namespace

SleepImageBackground::SleepImageBackground(int expected_width, int expected_height)
    : expected_width_(expected_width), expected_height_(expected_height) {}

SleepImageBackgroundResult SleepImageBackground::ObserveSubmission(const Frame& frame,
                                                                    bool accepted) {
  if (!accepted) return SleepImageBackgroundResult::kRejectedSubmission;
  if (frame.sleep_image_epoch != 0) return SleepImageBackgroundResult::kRejectedSleepImage;
  if (frame.sequence == 0) return SleepImageBackgroundResult::kRejectedStale;
  if (frame.dirty_rect.left != 0 || frame.dirty_rect.top != 0 ||
      frame.dirty_rect.right != expected_width_ || frame.dirty_rect.bottom != expected_height_) {
    return SleepImageBackgroundResult::kRejectedGeometry;
  }
  std::size_t expected_bytes = 0;
  if (!ExpectedBytes(expected_width_, expected_height_, &expected_bytes) ||
      !frame.grayscale_buffer || frame.grayscale_buffer->ByteCount() != expected_bytes ||
      frame.grayscale_buffer->Data() == nullptr) {
    return SleepImageBackgroundResult::kRejectedBytes;
  }
  if (!IsPanelGray(frame.grayscale_buffer->Data(), expected_bytes)) {
    return SleepImageBackgroundResult::kRejectedPanelGray;
  }
  std::lock_guard lock(mutex_);
  if (frame.sequence <= sequence_) return SleepImageBackgroundResult::kRejectedStale;
  panel_gray_.assign(frame.grayscale_buffer->Data(),
                     frame.grayscale_buffer->Data() + expected_bytes);
  sequence_ = frame.sequence;
  return SleepImageBackgroundResult::kRetained;
}

SleepImagePreparedOverlay SleepImageBackground::PrepareOverlay(
        std::span<const std::uint8_t> foreground_panel_gray,
        std::span<const std::uint8_t> foreground_alpha) const {
  std::lock_guard lock(mutex_);
  if (panel_gray_.empty()) {
    return {
            .result = SleepImageOverlayPreparationResult::kRejectedNoBackground,
            .panel_gray = {},
    };
  }
  SleepImageOverlayOutput composed = ComposeSleepImageOverlay(
          foreground_panel_gray, foreground_alpha, panel_gray_);
  if (composed.result != SleepImageOverlayResult::kComposited) {
    return {
            .result = SleepImageOverlayPreparationResult::kRejectedForeground,
            .panel_gray = {},
    };
  }
  return {
          .result = SleepImageOverlayPreparationResult::kComposited,
          .panel_gray = std::move(composed.panel_gray),
  };
}

void SleepImageBackground::Release() {
  std::lock_guard lock(mutex_);
  panel_gray_.clear();
  panel_gray_.shrink_to_fit();
  sequence_ = 0;
}

bool SleepImageBackground::available() const {
  std::lock_guard lock(mutex_);
  return !panel_gray_.empty();
}

std::uint64_t SleepImageBackground::sequence() const {
  std::lock_guard lock(mutex_);
  return sequence_;
}

}  // namespace neo2::eink
