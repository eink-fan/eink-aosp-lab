#include "eink_sleep_image_background.h"
#include "eink_software_grayscale.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {

using neo2::eink::Frame;
using neo2::eink::OwnedGrayscaleBuffer;
using neo2::eink::SleepImageBackground;
using neo2::eink::SleepImageBackgroundResult;
using neo2::eink::SleepImageOverlayPreparationResult;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

Frame Submission(std::uint64_t sequence, std::vector<std::uint8_t> pixels) {
  return {
          .sequence = sequence,
          .compose_buffer = nullptr,
          .source_buffer = nullptr,
          .grayscale_buffer = std::make_shared<OwnedGrayscaleBuffer>(2, 2, std::move(pixels)),
          .dirty_rect = {.left = 0, .top = 0, .right = 2, .bottom = 2},
  };
}

}  // namespace

int main() {
  SleepImageBackground background(2, 2);
  Frame pending = Submission(1, {0xf0, 0xe0, 0xd0, 0xc0});
  Require(background.ObserveSubmission(pending, false) ==
                  SleepImageBackgroundResult::kRejectedSubmission &&
                  !background.available(),
          "rejected/pending frame became the overlay background");

  Frame accepted = Submission(2, {0xf0, 0xe0, 0xd0, 0xc0});
  Require(background.ObserveSubmission(accepted, true) ==
                  SleepImageBackgroundResult::kRetained &&
                  background.available() && background.sequence() == 2,
          "accepted ordinary full-panel frame was not retained");

  Frame sleep = Submission(3, {0x10, 0x10, 0x10, 0x10});
  sleep.sleep_image_epoch = 7;
  Require(background.ObserveSubmission(sleep, true) ==
                  SleepImageBackgroundResult::kRejectedSleepImage &&
                  background.sequence() == 2,
          "sleep-image submission replaced the ordinary background");

  Frame stale = Submission(2, {0x20, 0x20, 0x20, 0x20});
  Require(background.ObserveSubmission(stale, true) ==
                  SleepImageBackgroundResult::kRejectedStale &&
                  background.sequence() == 2,
          "stale accepted frame replaced the newest background");

  Frame partial = Submission(4, {0x20, 0x20, 0x20, 0x20});
  partial.dirty_rect.right = 1;
  Require(background.ObserveSubmission(partial, true) ==
                  SleepImageBackgroundResult::kRejectedGeometry,
          "partial accepted frame replaced the full-panel background");

  Frame malformed = Submission(5, {0x21, 0x20, 0x20, 0x20});
  Require(background.ObserveSubmission(malformed, true) ==
                  SleepImageBackgroundResult::kRejectedPanelGray,
          "non-panel-gray accepted frame replaced the background");

  const auto overlay = background.PrepareOverlay(
          std::vector<std::uint8_t>{0x00, 0x00, 0x00, 0x00},
          std::vector<std::uint8_t>{0, 255, 128, 64});
  Require(overlay.result == SleepImageOverlayPreparationResult::kComposited &&
                  overlay.panel_gray == std::vector<std::uint8_t>({0xf0, 0x00, 0x70, 0x90}),
          "retained accepted frame did not produce the expected overlay");

  Require(background.PrepareOverlay(std::vector<std::uint8_t>{0x00},
                                    std::vector<std::uint8_t>{255})
                  .result == SleepImageOverlayPreparationResult::kRejectedForeground,
          "invalid foreground extent was accepted");

  background.Release();
  Require(!background.available() && background.sequence() == 0 &&
                  background.PrepareOverlay(
                          std::vector<std::uint8_t>{0x00, 0x00, 0x00, 0x00},
                          std::vector<std::uint8_t>{255, 255, 255, 255})
                                  .result ==
                          SleepImageOverlayPreparationResult::kRejectedNoBackground,
          "wake release retained page pixels");

  std::cout << "PASS\n";
  return 0;
}
