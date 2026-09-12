#pragma once

#include "eink_capture_admission_policy.h"

#include <cstddef>
#include <cstdint>

namespace neo2::eink {

// B001-R2's compact accounting model. It observes an existing capture path;
// it has no admission, scheduling, ownership, or output behavior of its own.
class CapturePressureAccounting final {
 public:
  struct PipelineState {
    bool conversion_in_flight = false;
    bool pending_capture = false;
    bool adapter_retained = false;
  };

  struct Diagnostics {
    std::uint64_t opportunities = 0;
    std::uint64_t no_retained_candidate = 0;
    std::uint64_t no_next_eligible_slot = 0;
    std::uint64_t elapsed_output_slot = 0;
    std::uint64_t closed_slot_skip = 0;
    std::uint64_t late_replacement = 0;
    std::uint64_t attempts = 0;
    std::uint64_t attempts_busy_zero = 0;
    std::uint64_t attempts_busy_one = 0;
    std::uint64_t attempts_busy_two_or_more = 0;
    std::uint64_t attempts_pool_full = 0;
    std::uint64_t full_pool_conversion_in_flight = 0;
    std::uint64_t full_pool_pending_capture = 0;
    std::uint64_t full_pool_adapter_retained = 0;
    std::uint64_t full_pool_deferred = 0;
    std::uint64_t full_pool_deferred_conversion_pending = 0;
    // A dispatched full-pool resample is not an ordinary admission
    // opportunity, but its TryCopy result remains an actual copy outcome.
    std::uint64_t resample_attempts = 0;
    std::uint64_t copied = 0;
    std::uint64_t pool_drops = 0;
    std::uint64_t copy_failures = 0;
    std::uint64_t conversion_samples = 0;
    std::uint64_t conversion_total_ns = 0;
    std::uint64_t conversion_max_ns = 0;
  };

  void RecordOpportunity(CaptureAdmissionReason reason);
  void RecordAttempt(std::size_t pool_busy, std::size_t pool_capacity,
                     const PipelineState& pipeline_state);
  // A B002 bounded-resample obligation replaces one doomed full-pool copy
  // attempt. It remains separately reconcilable from R2's actual attempts.
  void RecordFullPoolDeferred(const PipelineState& pipeline_state);
  // The later queueBuffer-only resample attempt bypasses ordinary admission.
  // Record it separately so normal admission and total copy outcomes both
  // reconcile without relabeling the resample as a fresh opportunity.
  void RecordResampleAttempt();
  void RecordCopied();
  void RecordPoolDrop();
  void RecordCopyFailure();
  void RecordConversion(std::uint64_t duration_ns);

  [[nodiscard]] Diagnostics diagnostics() const;
  [[nodiscard]] bool HasReconciledAccounting() const;

 private:
  Diagnostics diagnostics_;
};

}  // namespace neo2::eink
