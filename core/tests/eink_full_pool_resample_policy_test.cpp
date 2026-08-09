#include "eink_full_pool_resample_policy.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace {

using namespace std::chrono_literals;
using einklab::FullPoolCaptureDecision;
using einklab::FullPoolResampleConfig;
using einklab::FullPoolObservationDecision;
using einklab::FullPoolPipelineState;
using einklab::FullPoolReleaseDecision;
using einklab::FullPoolResamplePolicy;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  const auto start = std::chrono::steady_clock::time_point{};
  FullPoolResamplePolicy policy;
  auto invalid_config = FullPoolResampleConfig::Defaults();
  invalid_config.expiry = 50ms;
  Require(!policy.Reconfigure(invalid_config), "invalid expiry configuration was accepted");
  auto reconfigured = FullPoolResampleConfig::Defaults();
  reconfigured.expiry = 1s;
  Require(policy.Reconfigure(reconfigured), "valid expiry configuration was not applied");
  const FullPoolPipelineState qualifying{.conversion_in_flight = true,
                                          .pending_capture = true,
                                          .adapter_retained = false};
  Require(policy.ObserveFullPool(false, qualifying, start) == FullPoolObservationDecision::kIgnored,
          "non-full pool created an obligation");
  Require(policy.ObserveFullPool(true, {.conversion_in_flight = false,
                                         .pending_capture = true,
                                         .adapter_retained = false},
                                  start) == FullPoolObservationDecision::kIgnored,
          "conversion-free state escaped the narrow narrow predicate");
  Require(policy.ObserveFullPool(true, {.conversion_in_flight = true,
                                         .pending_capture = false,
                                         .adapter_retained = false},
                                  start) == FullPoolObservationDecision::kIgnored,
          "pending-free state escaped the narrow narrow predicate");
  Require(policy.ObserveFullPool(true, qualifying, start) == FullPoolObservationDecision::kCreated,
          "exact conversion-plus-pending state did not create an obligation");
  Require(policy.HasOutstandingObligation(), "created obligation was not retained as metadata");
  Require(policy.ObserveFullPool(true, qualifying, start + 1ms) ==
                  FullPoolObservationDecision::kCoalesced,
          "second full-pool observation created a second obligation");
  Require(policy.OnResourceReleased(start + 100ms) ==
                  FullPoolReleaseDecision::kDispatchResample,
          "resource release did not dispatch the bounded resample");
  Require(policy.HasOutstandingObligation(), "dispatched resample was not awaiting queueBuffer");
  Require(policy.TakeCaptureOpportunity() == FullPoolCaptureDecision::kResampleCapture,
          "first normal queueBuffer opportunity did not consume the resample");
  policy.RecordCaptureOutcome(FullPoolCaptureDecision::kResampleCapture, true);
  Require(!policy.HasOutstandingObligation(), "fulfilled resample left an outstanding obligation");

  Require(policy.ObserveFullPool(true, qualifying, start + 1s) ==
                  FullPoolObservationDecision::kCreated,
          "later exact full-pool state did not create a new single obligation");
  Require(policy.OnResourceReleased(start + 2s) ==
                  FullPoolReleaseDecision::kDispatchExpiryFallback,
          "expired obligation did not take the named fallback dispatch");
  Require(policy.TakeCaptureOpportunity() == FullPoolCaptureDecision::kExpiryFallbackCapture,
          "fallback did not bind to one queueBuffer opportunity");
  policy.RecordCaptureOutcome(FullPoolCaptureDecision::kExpiryFallbackCapture, false);
  Require(!policy.HasOutstandingObligation(), "failed fallback created a second outstanding request");

  const FullPoolPipelineState adapter_retained{.conversion_in_flight = true,
                                                .pending_capture = true,
                                                .adapter_retained = true};
  Require(policy.ObserveFullPool(true, adapter_retained, start + 3s) ==
                  FullPoolObservationDecision::kIgnored,
          "adapter-retained state escaped the narrow narrow predicate");
  Require(policy.OnResourceReleased(start + 3s) == FullPoolReleaseDecision::kNone,
          "non-qualifying state dispatched a resample");

  Require(policy.ObserveFullPool(true, qualifying, start + 4s) ==
                  FullPoolObservationDecision::kCreated,
          "final obligation was not created");
  policy.Cancel();
  Require(!policy.HasOutstandingObligation() &&
                  policy.OnResourceReleased(start + 4s) == FullPoolReleaseDecision::kNone,
          "cancellation left a request that could fire after shutdown");

  const auto diagnostics = policy.diagnostics();
  Require(diagnostics.created == 3 && diagnostics.coalesced == 1 && diagnostics.dispatched == 1 &&
                  diagnostics.expiry_fallback == 1 && diagnostics.fulfilled == 1 &&
                  diagnostics.copy_failures == 1 && diagnostics.cancelled == 1,
          "resample diagnostics did not reconcile the one-obligation flow");
  std::cout << "PASS\n";
  return 0;
}
