#pragma once

#include "eink_presentation_adapter.h"
#include "eink_sleep_image_overlay.h"

#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace neo2::eink {

enum class SleepImageBackgroundResult : std::uint8_t {
  kRetained,
  kRejectedSubmission,
  kRejectedSleepImage,
  kRejectedGeometry,
  kRejectedBytes,
  kRejectedPanelGray,
  kRejectedStale,
};

enum class SleepImageOverlayPreparationResult : std::uint8_t {
  kComposited,
  kRejectedNoBackground,
  kRejectedForeground,
};

struct SleepImagePreparedOverlay {
  SleepImageOverlayPreparationResult result =
          SleepImageOverlayPreparationResult::kRejectedNoBackground;
  std::vector<std::uint8_t> panel_gray;
};

// Owns only the latest accepted ordinary full-panel grayscale submission.
// Pending, rejected, lifecycle/sleep, malformed, and stale frames can never
// replace it. Release is used on wake so page pixels do not survive a sleep
// cycle or leave this native in-memory boundary.
class SleepImageBackground final {
 public:
  SleepImageBackground(int expected_width, int expected_height);

  [[nodiscard]] SleepImageBackgroundResult ObserveSubmission(const Frame& frame,
                                                              bool accepted);
  [[nodiscard]] SleepImagePreparedOverlay PrepareOverlay(
          std::span<const std::uint8_t> foreground_panel_gray,
          std::span<const std::uint8_t> foreground_alpha) const;
  void Release();

  [[nodiscard]] bool available() const;
  [[nodiscard]] std::uint64_t sequence() const;

 private:
  int expected_width_;
  int expected_height_;
  mutable std::mutex mutex_;
  std::uint64_t sequence_ = 0;
  std::vector<std::uint8_t> panel_gray_;
};

}  // namespace neo2::eink
