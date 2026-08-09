#include "eink_frame_demand_gate.h"

#include <cstdlib>
#include <iostream>

namespace {

using einklab::DemandDecision;
using einklab::FrameDemandGate;
using einklab::FrameSignature;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  FrameDemandGate gate;
  const FrameSignature first{.hash = 0x1111, .bytes = 4, .width = 2, .height = 2};
  Require(gate.Evaluate(first) == DemandDecision::kSessionStart,
          "first converted frame was not eligible");
  Require(gate.Evaluate(first) == DemandDecision::kUnchanged,
          "identical panel pixels were not suppressed");
  Require(gate.Evaluate({.hash = 0x2222, .bytes = 4, .width = 2, .height = 2}) ==
                  DemandDecision::kPanelContentChanged,
          "changed panel pixels were suppressed");
  Require(gate.Evaluate({.hash = 0x2222, .bytes = 8, .width = 4, .height = 2}) ==
                  DemandDecision::kPanelContentChanged,
          "geometry or byte-count change was suppressed despite equal hash");

  const auto diagnostics = gate.diagnostics();
  Require(diagnostics.session_start_frames == 1 && diagnostics.rearmed_frames == 0 &&
                  diagnostics.changed_frames == 2 && diagnostics.unchanged_frames == 1,
          "demand diagnostics are wrong");

  gate.Reset();
  Require(gate.Evaluate(first) == DemandDecision::kRearmed,
          "reset did not choose the rearm full-frame policy");
  std::cout << "PASS\n";
  return 0;
}
