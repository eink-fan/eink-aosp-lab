#include "eink_submission_observer.h"

namespace neo2::eink {

void SubmissionObserver::RecordAccepted(std::uint64_t submission_id, std::uint64_t submitted_at_ns) {
  ++diagnostics_.accepted_submissions;
  ++diagnostics_.completion_unknown_submissions;
  diagnostics_.latest_submission_id = submission_id;
  diagnostics_.latest_submit_ns = submitted_at_ns;
  diagnostics_.latest_timeout_elapsed_ns = 0;
  diagnostics_.pending_completion_unknown = true;
  latest_timeout_recorded_ = false;
}

bool SubmissionObserver::ObserveTimeout(std::uint64_t observed_at_ns, std::uint64_t timeout_ns) {
  if (!diagnostics_.pending_completion_unknown || latest_timeout_recorded_ || timeout_ns == 0 ||
      observed_at_ns < diagnostics_.latest_submit_ns ||
      observed_at_ns - diagnostics_.latest_submit_ns < timeout_ns) {
    return false;
  }
  ++diagnostics_.timeout_observations;
  diagnostics_.latest_timeout_elapsed_ns = observed_at_ns - diagnostics_.latest_submit_ns;
  latest_timeout_recorded_ = true;
  return true;
}

}  // namespace neo2::eink
