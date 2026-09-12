#include "eink_capture_admission_policy.h"

namespace neo2::eink {

CaptureAdmissionConfig CaptureAdmissionConfig::Defaults() {
  return {};
}

bool CaptureAdmissionConfig::IsValid() const {
  return late_sample_window >= std::chrono::milliseconds(25) &&
          late_sample_window <= std::chrono::milliseconds(250);
}

CaptureAdmissionPolicy::CaptureAdmissionPolicy(CaptureAdmissionConfig config) : config_(config) {
  if (!config_.IsValid()) config_ = CaptureAdmissionConfig::Defaults();
}

bool CaptureAdmissionPolicy::Reconfigure(const CaptureAdmissionConfig& config) {
  if (!config.IsValid()) return false;
  if (config_.late_sample_window == config.late_sample_window) return false;
  config_ = config;
  late_sampled_slot_.reset();
  return true;
}

CaptureAdmissionDecision CaptureAdmissionPolicy::Evaluate(
        bool retained_candidate,
        std::optional<std::chrono::steady_clock::time_point> next_eligible,
        std::chrono::steady_clock::time_point now) {
  if (!retained_candidate) {
    last_reason_ = CaptureAdmissionReason::kNoRetainedCandidate;
    ++diagnostics_.capture_allowed;
    return CaptureAdmissionDecision::kCapture;
  }
  if (!next_eligible) {
    last_reason_ = CaptureAdmissionReason::kNoNextEligibleSlot;
    ++diagnostics_.capture_allowed;
    return CaptureAdmissionDecision::kCapture;
  }
  if (now >= *next_eligible) {
    last_reason_ = CaptureAdmissionReason::kElapsedOutputSlot;
    ++diagnostics_.capture_allowed;
    return CaptureAdmissionDecision::kCapture;
  }
  if (*next_eligible - now <= config_.late_sample_window && late_sampled_slot_ != next_eligible) {
    late_sampled_slot_ = next_eligible;
    last_reason_ = CaptureAdmissionReason::kLateReplacement;
    ++diagnostics_.late_sample_taken;
    return CaptureAdmissionDecision::kCaptureLateReplacement;
  }
  last_reason_ = CaptureAdmissionReason::kClosedSlotSkip;
  ++diagnostics_.capture_skipped_retained;
  return CaptureAdmissionDecision::kSkipRetainedCandidate;
}

CaptureAdmissionReason CaptureAdmissionPolicy::last_reason() const {
  return last_reason_;
}

CaptureAdmissionPolicy::Diagnostics CaptureAdmissionPolicy::diagnostics() const {
  return diagnostics_;
}

}  // namespace neo2::eink
