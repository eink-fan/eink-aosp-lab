#include "eink_capture_admission_policy.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace {

using namespace std::chrono_literals;
using einklab::CaptureAdmissionDecision;
using einklab::CaptureAdmissionReason;
using einklab::CaptureAdmissionPolicy;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  const auto now = std::chrono::steady_clock::time_point{};
  CaptureAdmissionPolicy policy;
  Require(!policy.Reconfigure(einklab::CaptureAdmissionConfig::Defaults()),
          "equivalent late-sample configuration was reported as a change");
  Require(!policy.Reconfigure({.late_sample_window = 10ms}),
          "invalid late-sample configuration was accepted");

  Require(policy.Evaluate(false, now + 500ms, now) == CaptureAdmissionDecision::kCapture,
          "a missing retained candidate blocked the leading sample");
  Require(policy.last_reason() == CaptureAdmissionReason::kNoRetainedCandidate,
          "leading-sample reason is wrong");
  Require(policy.Evaluate(true, std::nullopt, now) == CaptureAdmissionDecision::kCapture,
          "a missing next eligible slot blocked capture");
  Require(policy.last_reason() == CaptureAdmissionReason::kNoNextEligibleSlot,
          "missing-slot reason is wrong");
  Require(policy.Evaluate(true, now, now) == CaptureAdmissionDecision::kCapture,
          "an elapsed output slot blocked capture");
  Require(policy.last_reason() == CaptureAdmissionReason::kElapsedOutputSlot,
          "elapsed-slot reason is wrong");
  Require(policy.Evaluate(true, now + 500ms, now) ==
                  CaptureAdmissionDecision::kSkipRetainedCandidate,
          "a closed slot did not skip a redundant snapshot");
  Require(policy.last_reason() == CaptureAdmissionReason::kClosedSlotSkip,
          "closed-slot reason is wrong");
  Require(policy.Evaluate(true, now + 90ms, now) ==
                  CaptureAdmissionDecision::kCaptureLateReplacement,
          "late replacement did not open inside the late-sample window");
  Require(policy.last_reason() == CaptureAdmissionReason::kLateReplacement,
          "late-replacement reason is wrong");
  Require(policy.Evaluate(true, now + 90ms, now + 10ms) ==
                  CaptureAdmissionDecision::kSkipRetainedCandidate,
          "a slot accepted more than one late replacement");
  Require(policy.Evaluate(true, now + 90ms, now + 100ms) == CaptureAdmissionDecision::kCapture,
          "the no-late-sample fallback did not leave the retained candidate usable");
  Require(policy.Evaluate(true, now + 600ms, now + 110ms) ==
                  CaptureAdmissionDecision::kSkipRetainedCandidate,
          "a new closed slot did not resume normal skip behaviour");
  Require(policy.Evaluate(true, now + 200ms, now + 110ms) ==
                  CaptureAdmissionDecision::kCaptureLateReplacement,
          "a new output slot could not take its own late replacement");

  const auto diagnostics = policy.diagnostics();
  Require(diagnostics.capture_allowed == 4 && diagnostics.capture_skipped_retained == 3 &&
                  diagnostics.late_sample_taken == 2,
          "admission reason counters are wrong");
  std::cout << "PASS\n";
  return 0;
}
