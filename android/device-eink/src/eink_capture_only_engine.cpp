#include "eink_capture_only_engine.h"

#include <utility>

namespace neo2::eink {

CaptureOnlyEngine::CaptureOnlyEngine(SubmissionObserver observer) : observer_(std::move(observer)) {}

bool CaptureOnlyEngine::Submit(const Frame& frame) {
  CaptureOnlySubmission submission{
          .sequence = frame.sequence,
          .dirty_rect = frame.dirty_rect,
          .engine_mode = frame.engine_mode,
          .policy_flag = frame.policy_flag,
          .handwriting = frame.handwriting,
          .has_grayscale_staging = static_cast<bool>(frame.grayscale_buffer),
  };
  {
    std::lock_guard lock(mutex_);
    ++submission_count_;
    latest_submission_ = submission;
  }
  if (observer_) {
    observer_(submission);
  }
  return true;
}

std::uint64_t CaptureOnlyEngine::SubmissionCount() const {
  std::lock_guard lock(mutex_);
  return submission_count_;
}

std::optional<CaptureOnlySubmission> CaptureOnlyEngine::LatestSubmission() const {
  std::lock_guard lock(mutex_);
  return latest_submission_;
}

}  // namespace neo2::eink
