#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace neo2::eink {

// B001's capture-side companion to the depth-one trailing scheduler. It never
// blocks a submission: when a newest converted candidate already exists for a
// closed output slot, it merely avoids wasting more snapshot-pool attempts.
struct CaptureAdmissionConfig {
  std::chrono::milliseconds late_sample_window{100};

  [[nodiscard]] static CaptureAdmissionConfig Defaults();
  [[nodiscard]] bool IsValid() const;
};

enum class CaptureAdmissionDecision : std::uint8_t {
  kCapture,
  kSkipRetainedCandidate,
  kCaptureLateReplacement,
};

// Why the current natural capture opportunity received its admission result.
// This describes only the existing B001 decision; it does not alter it.
enum class CaptureAdmissionReason : std::uint8_t {
  kNoRetainedCandidate,
  kNoNextEligibleSlot,
  kElapsedOutputSlot,
  kClosedSlotSkip,
  kLateReplacement,
};

class CaptureAdmissionPolicy final {
 public:
  struct Diagnostics {
    std::uint64_t capture_allowed = 0;
    std::uint64_t capture_skipped_retained = 0;
    std::uint64_t late_sample_taken = 0;
  };

  explicit CaptureAdmissionPolicy(CaptureAdmissionConfig config = CaptureAdmissionConfig::Defaults());

  [[nodiscard]] bool Reconfigure(const CaptureAdmissionConfig& config);

  // `next_eligible` is the existing scheduler's next queue-acceptance slot.
  // With no retained candidate or no closed slot, a normal sample is allowed.
  // Each unique slot admits at most one late replacement sample.
  [[nodiscard]] CaptureAdmissionDecision Evaluate(
          bool retained_candidate,
          std::optional<std::chrono::steady_clock::time_point> next_eligible,
          std::chrono::steady_clock::time_point now);
  [[nodiscard]] CaptureAdmissionReason last_reason() const;
  [[nodiscard]] Diagnostics diagnostics() const;

 private:
  CaptureAdmissionConfig config_;
  std::optional<std::chrono::steady_clock::time_point> late_sampled_slot_;
  CaptureAdmissionReason last_reason_ = CaptureAdmissionReason::kNoRetainedCandidate;
  Diagnostics diagnostics_;
};

}  // namespace neo2::eink
