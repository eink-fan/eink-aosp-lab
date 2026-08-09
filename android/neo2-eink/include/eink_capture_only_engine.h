#pragma once

#include "eink_presentation_adapter.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

namespace neo2::eink {

// Bounded metadata that a capture-only Android integration may inspect without
// retaining frame buffers or invoking a panel backend.
struct CaptureOnlySubmission {
  std::uint64_t sequence = 0;
  Rect dirty_rect;
  int engine_mode = 0;
  int policy_flag = 0;
  bool handwriting = false;
  bool has_grayscale_staging = false;
};

// Deliberately inert Engine implementation for the first SurfaceFlinger
// capture proof. It is thread-safe for a diagnostics reader, retains only the
// latest small metadata record, and has no vendor-library or device access.
class CaptureOnlyEngine final : public Engine {
 public:
  using SubmissionObserver = std::function<void(const CaptureOnlySubmission&)>;

  // The observer receives only bounded submission metadata after the engine
  // records it. It is optional so host users retain the inert default path.
  explicit CaptureOnlyEngine(SubmissionObserver observer = {});

  bool Submit(const Frame& frame) override;

  [[nodiscard]] std::uint64_t SubmissionCount() const;
  [[nodiscard]] std::optional<CaptureOnlySubmission> LatestSubmission() const;

 private:
  mutable std::mutex mutex_;
  std::uint64_t submission_count_ = 0;
  std::optional<CaptureOnlySubmission> latest_submission_;
  SubmissionObserver observer_;
};

}  // namespace neo2::eink
