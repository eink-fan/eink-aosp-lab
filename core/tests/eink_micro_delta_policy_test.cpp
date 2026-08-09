#include "eink_micro_delta_policy.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>

namespace {

using namespace std::chrono_literals;
using einklab::GrayscaleDelta;
using einklab::MicroDeltaDecision;
using einklab::MicroDeltaPolicy;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

GrayscaleDelta Micro(int left = 10, int top = 1'050) {
  return {.changed_pixels = 74,
          .total_pixels = 1448ULL * 1072ULL,
          .left = left,
          .top = top,
          .right = left + 8,
          .bottom = top + 10};
}

}  // namespace

int main() {
  const auto start = std::chrono::steady_clock::time_point{};
  MicroDeltaPolicy policy;
  Require(!policy.Reconfigure(einklab::MicroDeltaConfig::Defaults()),
          "equivalent configuration was reported as a change");
  auto invalid_config = einklab::MicroDeltaConfig::Defaults();
  invalid_config.changed_ppm = 99;
  Require(!policy.Reconfigure(invalid_config), "invalid micro configuration was accepted");
  auto configured = einklab::MicroDeltaConfig::Defaults();
  configured.settle = 1'000ms;
  Require(policy.Reconfigure(configured), "valid micro configuration was not applied");
  Require(policy.Reconfigure(einklab::MicroDeltaConfig::Defaults()),
          "default micro configuration was not restored");

  Require(policy.Evaluate(Micro(), 1448, 1072, start) == MicroDeltaDecision::kHeld,
          "first lower-edge chrome delta was not held");
  Require(policy.HasDeferredCandidate() && policy.Deadline() == start + 300ms,
          "settle deadline did not use the configured default");
  Require(policy.Evaluate(Micro(14, 1'052), 1448, 1072, start + 50ms) ==
                  MicroDeltaDecision::kHeld,
          "equivalent delta did not coalesce during settle");
  policy.MarkExpirySubmitted(start + 300ms);
  Require(!policy.HasDeferredCandidate(), "expiry submit retained a stale candidate");
  Require(policy.Evaluate(Micro(16, 1'053), 1448, 1072, start + 400ms) ==
                  MicroDeltaDecision::kRepeatSuppressed,
          "equivalent recurring micro delta was not suppressed");
  Require(policy.HasDeferredCandidate() && policy.Deadline() == start + 2'300ms,
          "repeat expiry window is wrong");

  Require(policy.Evaluate(Micro(100, 1'050), 1448, 1072, start + 500ms) ==
                  MicroDeltaDecision::kSubmitPromoted,
          "material movement did not promote immediately");
  Require(!policy.HasDeferredCandidate(), "promotion retained the old micro candidate");

  const GrayscaleDelta broad{.changed_pixels = 10'000,
                             .total_pixels = 1448ULL * 1072ULL,
                             .left = 0,
                             .top = 0,
                             .right = 300,
                             .bottom = 100};
  Require(policy.Evaluate(broad, 1448, 1072, start + 600ms) ==
                  MicroDeltaDecision::kSubmitSubstantive,
          "substantive UI result was deferred");
  Require(policy.Evaluate(Micro(10, 150), 1448, 1072, start + 700ms) ==
                  MicroDeltaDecision::kSubmitSubstantive,
          "non-chrome locality was classified as micro");
  Require(policy.Evaluate(std::nullopt, 1448, 1072, start + 800ms) ==
                  MicroDeltaDecision::kSubmitSubstantive,
          "unavailable delta did not fail open");

  const auto diagnostics = policy.diagnostics();
  Require(diagnostics.held == 2 && diagnostics.repeat_suppressed == 1 &&
                  diagnostics.promoted == 1 && diagnostics.expiry_submitted == 1 &&
                  diagnostics.substantive_bypass == 3,
          "policy reason counters are incomplete");

  einklab::MicroDeltaConfig top_edge = einklab::MicroDeltaConfig::Defaults();
  top_edge.chrome_edge = einklab::MicroChromeEdge::kTop;
  MicroDeltaPolicy top_policy(top_edge);
  Require(top_policy.Evaluate(Micro(10, 10), 1448, 1072, start) == MicroDeltaDecision::kHeld,
          "explicit top-edge configuration did not classify its boundary");
  Require(top_policy.Evaluate(Micro(), 1448, 1072, start + 1ms) ==
                  MicroDeltaDecision::kSubmitSubstantive,
          "opposite edge was admitted by the top-edge configuration");
  std::cout << "PASS\n";
  return 0;
}
