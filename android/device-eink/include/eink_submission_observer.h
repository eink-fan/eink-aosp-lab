#pragma once

#include <cstdint>

namespace neo2::eink {

// Tracks only the absence of a proven completion callback. A timeout
// observation is diagnostic: it never clears pending state or permits buffer
// reuse.
class SubmissionObserver final {
 public:
  struct Diagnostics {
    std::uint64_t accepted_submissions = 0;
    std::uint64_t completion_unknown_submissions = 0;
    std::uint64_t timeout_observations = 0;
    std::uint64_t latest_submission_id = 0;
    std::uint64_t latest_submit_ns = 0;
    std::uint64_t latest_timeout_elapsed_ns = 0;
    bool pending_completion_unknown = false;
  };

  void RecordAccepted(std::uint64_t submission_id, std::uint64_t submitted_at_ns);
  [[nodiscard]] bool ObserveTimeout(std::uint64_t observed_at_ns,
                                    std::uint64_t timeout_ns);
  [[nodiscard]] Diagnostics diagnostics() const { return diagnostics_; }

 private:
  Diagnostics diagnostics_;
  bool latest_timeout_recorded_ = false;
};

}  // namespace neo2::eink
