#include "eink_one_shot_engine.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

using neo2::eink::ComposeBuffer;
using neo2::eink::Engine;
using neo2::eink::Frame;
using neo2::eink::OneShotEngine;
using neo2::eink::Rect;
using neo2::eink::RefreshMode;
using neo2::eink::ToEngineMode;

class Buffer final : public ComposeBuffer {};

class RecordingEngine final : public Engine {
 public:
  bool Submit(const Frame& frame) override {
    ++submissions;
    sequence = frame.sequence;
    return result;
  }

  int submissions = 0;
  std::uint64_t sequence = 0;
  bool result = true;
};

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

Frame ObservedFrame(const std::shared_ptr<Buffer>& buffer) {
  return Frame{
          .sequence = 7,
          .compose_buffer = buffer,
          .source_buffer = nullptr,
          .grayscale_buffer = nullptr,
          .dirty_rect = Rect{.left = 0, .top = 0, .right = 1448, .bottom = 1072},
          .engine_mode = ToEngineMode(RefreshMode::kGc16Partial) +
                  ToEngineMode(RefreshMode::kFullUpdate),
          .policy_flag = 0,
          .handwriting = false,
  };
}

}  // namespace

int main() {
  auto buffer = std::make_shared<Buffer>();
  RecordingEngine vendor;
  OneShotEngine gate(vendor);
  const Frame observed = ObservedFrame(buffer);

  Require(!gate.Submit(observed), "disarmed gate submitted a frame");
  Require(vendor.submissions == 0, "delegate was called while disarmed");
  Require(gate.Arm(), "first arm failed");
  Require(!gate.Arm(), "second arm succeeded");

  Frame wrong_mode = observed;
  wrong_mode.engine_mode = ToEngineMode(RefreshMode::kGc16Partial);
  Require(!gate.Submit(wrong_mode), "partial mode passed one-shot gate");
  Require(vendor.submissions == 0, "invalid frame reached delegate");

  Require(gate.Submit(observed), "observed full-frame submission failed");
  Require(vendor.submissions == 1 && vendor.sequence == 7, "delegate did not receive one frame");
  Require(!gate.Submit(observed), "terminal gate retried a completed attempt");
  Require(vendor.submissions == 1, "delegate saw more than one frame");

  const auto diagnostics = gate.diagnostics();
  Require(diagnostics.state == OneShotEngine::State::kAttempted, "gate did not become terminal");
  Require(diagnostics.attempts == 1 && diagnostics.delegate_accepted,
          "attempt diagnostics are incorrect");
  Require(diagnostics.rejected == 3, "rejection diagnostics are incorrect");

  RecordingEngine rejecting_vendor;
  rejecting_vendor.result = false;
  OneShotEngine rejecting_gate(rejecting_vendor);
  Require(rejecting_gate.Arm(), "failure-path gate did not arm");
  Require(!rejecting_gate.Submit(observed), "delegate failure was not propagated");
  Require(!rejecting_gate.Submit(observed), "failed attempt was retried");
  const auto failure_diagnostics = rejecting_gate.diagnostics();
  Require(rejecting_vendor.submissions == 1 && failure_diagnostics.attempts == 1 &&
                  !failure_diagnostics.delegate_accepted,
          "failed attempt was not terminally recorded");
  std::cout << "PASS\n";
  return 0;
}