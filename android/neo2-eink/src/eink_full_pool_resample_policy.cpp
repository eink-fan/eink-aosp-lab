#include "eink_full_pool_resample_policy.h"

namespace neo2::eink {

FullPoolResampleConfig FullPoolResampleConfig::Defaults() {
  return {};
}

bool FullPoolResampleConfig::IsValid() const {
  return expiry >= std::chrono::milliseconds(100) && expiry <= std::chrono::seconds(2);
}

FullPoolResamplePolicy::FullPoolResamplePolicy(FullPoolResampleConfig config) : config_(config) {
  if (!config_.IsValid()) config_ = FullPoolResampleConfig::Defaults();
}

bool FullPoolResamplePolicy::Reconfigure(const FullPoolResampleConfig& config) {
  if (!config.IsValid() || config.expiry == config_.expiry) return false;
  config_ = config;
  Cancel();
  return true;
}

FullPoolObservationDecision FullPoolResamplePolicy::ObserveFullPool(
        bool pool_full, const FullPoolPipelineState& state,
        std::chrono::steady_clock::time_point now) {
  const bool qualifies = pool_full && state.conversion_in_flight && state.pending_capture &&
          !state.adapter_retained;
  if (!qualifies) return FullPoolObservationDecision::kIgnored;
  if (outstanding_ || awaiting_capture_) {
    ++diagnostics_.coalesced;
    return FullPoolObservationDecision::kCoalesced;
  }
  outstanding_ = true;
  deadline_ = now + config_.expiry;
  ++diagnostics_.created;
  return FullPoolObservationDecision::kCreated;
}

FullPoolReleaseDecision FullPoolResamplePolicy::OnResourceReleased(
        std::chrono::steady_clock::time_point now) {
  if (!outstanding_) return FullPoolReleaseDecision::kNone;
  outstanding_ = false;
  awaiting_capture_ = true;
  awaiting_expiry_fallback_ = now >= deadline_;
  if (awaiting_expiry_fallback_) {
    ++diagnostics_.expiry_fallback;
    return FullPoolReleaseDecision::kDispatchExpiryFallback;
  }
  ++diagnostics_.dispatched;
  return FullPoolReleaseDecision::kDispatchResample;
}

FullPoolCaptureDecision FullPoolResamplePolicy::TakeCaptureOpportunity() {
  if (!awaiting_capture_) return FullPoolCaptureDecision::kNormalCapture;
  awaiting_capture_ = false;
  return awaiting_expiry_fallback_ ? FullPoolCaptureDecision::kExpiryFallbackCapture
                                   : FullPoolCaptureDecision::kResampleCapture;
}

void FullPoolResamplePolicy::RecordCaptureOutcome(FullPoolCaptureDecision decision, bool copied) {
  if (decision == FullPoolCaptureDecision::kNormalCapture) return;
  awaiting_expiry_fallback_ = false;
  if (copied) {
    ++diagnostics_.fulfilled;
  } else {
    ++diagnostics_.copy_failures;
  }
}

void FullPoolResamplePolicy::Cancel() {
  if (outstanding_ || awaiting_capture_) ++diagnostics_.cancelled;
  outstanding_ = false;
  awaiting_capture_ = false;
  awaiting_expiry_fallback_ = false;
}

bool FullPoolResamplePolicy::HasOutstandingObligation() const {
  return outstanding_ || awaiting_capture_;
}

FullPoolResamplePolicy::Diagnostics FullPoolResamplePolicy::diagnostics() const {
  return diagnostics_;
}

}  // namespace neo2::eink
