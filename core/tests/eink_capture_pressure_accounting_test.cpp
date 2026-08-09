#include "eink_capture_pressure_accounting.h"

#include <cstdlib>
#include <iostream>

namespace {

using einklab::CaptureAdmissionReason;
using einklab::CapturePressureAccounting;

void Require(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

}  // namespace

int main() {
  CapturePressureAccounting accounting;
  accounting.RecordOpportunity(CaptureAdmissionReason::kNoRetainedCandidate);
  accounting.RecordAttempt(0, 2, {});
  accounting.RecordCopied();

  accounting.RecordOpportunity(CaptureAdmissionReason::kNoNextEligibleSlot);
  accounting.RecordAttempt(1, 2, {.conversion_in_flight = true});
  accounting.RecordCopied();

  accounting.RecordOpportunity(CaptureAdmissionReason::kElapsedOutputSlot);
  accounting.RecordAttempt(2, 2, {.conversion_in_flight = true, .pending_capture = true,
                                  .adapter_retained = true});
  accounting.RecordPoolDrop();

  accounting.RecordOpportunity(CaptureAdmissionReason::kLateReplacement);
  accounting.RecordAttempt(2, 2, {.pending_capture = true});
  accounting.RecordCopyFailure();

  accounting.RecordOpportunity(CaptureAdmissionReason::kClosedSlotSkip);
  accounting.RecordOpportunity(CaptureAdmissionReason::kNoNextEligibleSlot);
  accounting.RecordFullPoolDeferred(
          {.conversion_in_flight = true, .pending_capture = true, .adapter_retained = false});
  accounting.RecordResampleAttempt();
  accounting.RecordCopied();
  accounting.RecordConversion(14);
  accounting.RecordConversion(7);

  const auto diagnostics = accounting.diagnostics();
  Require(accounting.HasReconciledAccounting(), "accounting did not reconcile");
  Require(diagnostics.opportunities == 6 && diagnostics.attempts == 4,
          "opportunity/admission totals are wrong");
  Require(diagnostics.attempts_busy_zero == 1 && diagnostics.attempts_busy_one == 1 &&
                  diagnostics.attempts_busy_two_or_more == 2,
          "pool occupancy totals are wrong");
  Require(diagnostics.attempts_pool_full == 2 &&
                  diagnostics.full_pool_conversion_in_flight == 1 &&
                  diagnostics.full_pool_pending_capture == 2 &&
                  diagnostics.full_pool_adapter_retained == 1,
          "full-pool state accounting is wrong");
  Require(diagnostics.full_pool_deferred == 1 &&
                  diagnostics.full_pool_deferred_conversion_pending == 1,
          "bounded resample deferral accounting is wrong");
  Require(diagnostics.resample_attempts == 1, "resample attempt accounting is wrong");
  Require(diagnostics.copied == 3 && diagnostics.pool_drops == 1 &&
                  diagnostics.copy_failures == 1,
          "attempt outcome accounting is wrong");
  Require(diagnostics.conversion_samples == 2 && diagnostics.conversion_total_ns == 21 &&
                  diagnostics.conversion_max_ns == 14,
          "conversion accounting is wrong");

  std::cout << "PASS\n";
  return 0;
}
