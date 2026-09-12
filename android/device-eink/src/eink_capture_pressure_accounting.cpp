#include "eink_capture_pressure_accounting.h"

#include <algorithm>

namespace neo2::eink {

void CapturePressureAccounting::RecordOpportunity(CaptureAdmissionReason reason) {
  ++diagnostics_.opportunities;
  switch (reason) {
    case CaptureAdmissionReason::kNoRetainedCandidate:
      ++diagnostics_.no_retained_candidate;
      break;
    case CaptureAdmissionReason::kNoNextEligibleSlot:
      ++diagnostics_.no_next_eligible_slot;
      break;
    case CaptureAdmissionReason::kElapsedOutputSlot:
      ++diagnostics_.elapsed_output_slot;
      break;
    case CaptureAdmissionReason::kClosedSlotSkip:
      ++diagnostics_.closed_slot_skip;
      break;
    case CaptureAdmissionReason::kLateReplacement:
      ++diagnostics_.late_replacement;
      break;
  }
}

void CapturePressureAccounting::RecordAttempt(std::size_t pool_busy, std::size_t pool_capacity,
                                              const PipelineState& pipeline_state) {
  ++diagnostics_.attempts;
  if (pool_busy == 0) {
    ++diagnostics_.attempts_busy_zero;
  } else if (pool_busy == 1) {
    ++diagnostics_.attempts_busy_one;
  } else {
    ++diagnostics_.attempts_busy_two_or_more;
  }
  if (pool_capacity == 0 || pool_busy < pool_capacity) return;
  ++diagnostics_.attempts_pool_full;
  if (pipeline_state.conversion_in_flight) ++diagnostics_.full_pool_conversion_in_flight;
  if (pipeline_state.pending_capture) ++diagnostics_.full_pool_pending_capture;
  if (pipeline_state.adapter_retained) ++diagnostics_.full_pool_adapter_retained;
}

void CapturePressureAccounting::RecordFullPoolDeferred(const PipelineState& pipeline_state) {
  ++diagnostics_.full_pool_deferred;
  if (pipeline_state.conversion_in_flight && pipeline_state.pending_capture &&
      !pipeline_state.adapter_retained) {
    ++diagnostics_.full_pool_deferred_conversion_pending;
  }
}

void CapturePressureAccounting::RecordResampleAttempt() {
  ++diagnostics_.resample_attempts;
}

void CapturePressureAccounting::RecordCopied() {
  ++diagnostics_.copied;
}

void CapturePressureAccounting::RecordPoolDrop() {
  ++diagnostics_.pool_drops;
}

void CapturePressureAccounting::RecordCopyFailure() {
  ++diagnostics_.copy_failures;
}

void CapturePressureAccounting::RecordConversion(std::uint64_t duration_ns) {
  ++diagnostics_.conversion_samples;
  diagnostics_.conversion_total_ns += duration_ns;
  diagnostics_.conversion_max_ns = std::max(diagnostics_.conversion_max_ns, duration_ns);
}

CapturePressureAccounting::Diagnostics CapturePressureAccounting::diagnostics() const {
  return diagnostics_;
}

bool CapturePressureAccounting::HasReconciledAccounting() const {
  const auto admissions = diagnostics_.no_retained_candidate + diagnostics_.no_next_eligible_slot +
          diagnostics_.elapsed_output_slot + diagnostics_.late_replacement;
  const auto reasons = admissions + diagnostics_.closed_slot_skip;
  const auto occupancy = diagnostics_.attempts_busy_zero + diagnostics_.attempts_busy_one +
          diagnostics_.attempts_busy_two_or_more;
  const auto outcomes = diagnostics_.copied + diagnostics_.pool_drops + diagnostics_.copy_failures;
  return diagnostics_.opportunities == reasons &&
          diagnostics_.opportunities == diagnostics_.attempts +
                  diagnostics_.closed_slot_skip + diagnostics_.full_pool_deferred &&
          diagnostics_.attempts == occupancy &&
          diagnostics_.attempts + diagnostics_.resample_attempts == outcomes;
}

}  // namespace neo2::eink
