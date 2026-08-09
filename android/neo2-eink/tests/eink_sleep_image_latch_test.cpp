#include "eink_sleep_image_latch.h"
#include "eink_software_grayscale.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace {

using neo2::eink::Frame;
using neo2::eink::OwnedGrayscaleBuffer;
using neo2::eink::SleepImageCandidate;
using neo2::eink::SleepImageDecision;
using neo2::eink::SleepImageLatch;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::shared_ptr<OwnedGrayscaleBuffer> Pixels(int width, int height,
                                             std::vector<std::uint8_t> pixels) {
  return std::make_shared<OwnedGrayscaleBuffer>(width, height, std::move(pixels));
}

Frame FullFrame(const std::shared_ptr<OwnedGrayscaleBuffer>& pixels, int width = 2,
                int height = 2) {
  return Frame{.compose_buffer = {},
               .source_buffer = {},
               .grayscale_buffer = pixels,
               .dirty_rect = {.left = 0, .top = 0, .right = width, .bottom = height}};
}

SleepImageCandidate Candidate(const std::shared_ptr<OwnedGrayscaleBuffer>& pixels,
                              int width = 2, int height = 2) {
  return {.buffer = pixels, .width = width, .height = height};
}

}  // namespace

int main() {
  const auto black = Pixels(2, 2, {0, 0, 0, 0});
  const auto sleep = Pixels(2, 2, {0x10, 0x20, 0x30, 0x40});
  const auto awake = Pixels(2, 2, {0, 0, 0x10, 0});

  SleepImageLatch latch;
  int selector_calls = 0;
  auto selector = [&](int width, int height) {
    ++selector_calls;
    Require(width == 2 && height == 2, "selector received wrong geometry");
    return Candidate(sleep);
  };

  Frame first = FullFrame(black);
  Require(latch.Evaluate(first, true, selector) == SleepImageDecision::kReplace,
          "first armed black frame was not replaced");
  Require(first.grayscale_buffer == sleep && latch.selection_latched(),
          "selected sleep image was not installed and latched");

  Frame repeated = FullFrame(black);
  Require(latch.Evaluate(repeated, true, selector) == SleepImageDecision::kSuppress,
          "repeated black frame was not suppressed");
  Require(selector_calls == 1 && repeated.grayscale_buffer == black,
          "repeated black frame selected or mutated again");

  Require(latch.Evaluate(repeated, false, selector) == SleepImageDecision::kSuppress,
          "wake-time black frame was not suppressed");
  Require(latch.selection_latched() && selector_calls == 1,
          "arm transition cancelled or replayed the selection");

  Frame nonblack = FullFrame(awake);
  Require(latch.Evaluate(nonblack, false, selector) == SleepImageDecision::kPassThrough,
          "nonblack frame did not pass through");
  Require(!latch.selection_latched(), "nonblack frame did not clear selection latch");
  Frame next_cycle = FullFrame(black);
  Require(latch.Evaluate(next_cycle, true, selector) == SleepImageDecision::kReplace &&
                  selector_calls == 2,
          "following sleep cycle did not select exactly once");

  SleepImageLatch unarmed;
  int unarmed_calls = 0;
  Frame unarmed_black = FullFrame(black);
  Require(unarmed.Evaluate(unarmed_black, false,
                           [&](int, int) {
                             ++unarmed_calls;
                             return Candidate(sleep);
                           }) == SleepImageDecision::kPassThrough &&
                  unarmed_calls == 0,
          "unarmed black frame invoked the selector");

  SleepImageLatch failures;
  Frame failed = FullFrame(black);
  Require(failures.Evaluate(failed, true, {}) == SleepImageDecision::kPassThrough,
          "missing selector did not fail closed");
  int failed_selector_calls = 0;
  Require(failures.Evaluate(failed, true,
                            [&](int, int) {
                              ++failed_selector_calls;
                              return Candidate(sleep);
                            }) == SleepImageDecision::kSuppress,
          "failed cycle retried after a missing selector");
  Require(failed_selector_calls == 0,
          "failed cycle invoked a later selector");
  Require(!failures.selection_latched(), "invalid candidate latched a selection");

  Frame failure_reset = FullFrame(awake);
  Require(failures.Evaluate(failure_reset, false, selector) ==
                  SleepImageDecision::kPassThrough,
          "nonblack frame did not reset failed cycle");
  Frame after_failure = FullFrame(black);
  Require(failures.Evaluate(after_failure, true, selector) == SleepImageDecision::kReplace,
          "selection did not recover after failed-cycle reset");

  SleepImageLatch invalid_candidate;
  int invalid_calls = 0;
  Frame invalid_candidate_frame = FullFrame(black);
  const auto short_sleep = Pixels(2, 2, {0x10, 0x20, 0x30});
  Require(invalid_candidate.Evaluate(
                  invalid_candidate_frame, true,
                  [&](int, int) {
                    ++invalid_calls;
                    return Candidate(short_sleep);
                  }) == SleepImageDecision::kPassThrough,
          "candidate with lying geometry did not use its actual byte extent");
  Require(invalid_candidate.Evaluate(invalid_candidate_frame, true, selector) ==
                  SleepImageDecision::kSuppress &&
                  invalid_calls == 1,
          "invalid candidate was retried in the same cycle");

  SleepImageLatch null_candidate;
  int null_candidate_calls = 0;
  Frame null_candidate_frame = FullFrame(black);
  Require(null_candidate.Evaluate(
                  null_candidate_frame, true,
                  [&](int, int) {
                    ++null_candidate_calls;
                    return SleepImageCandidate{};
                  }) == SleepImageDecision::kPassThrough,
          "null candidate did not fail closed");
  Require(null_candidate.Evaluate(null_candidate_frame, true, selector) ==
                  SleepImageDecision::kSuppress &&
                  null_candidate_calls == 1,
          "null candidate was retried in the same cycle");

  SleepImageLatch mismatched_candidate;
  int mismatch_calls = 0;
  Frame mismatch_frame = FullFrame(black);
  Require(mismatched_candidate.Evaluate(
                  mismatch_frame, true,
                  [&](int, int) {
                    ++mismatch_calls;
                    return Candidate(sleep, 1, 4);
                  }) == SleepImageDecision::kPassThrough,
          "geometry-mismatched candidate did not fail closed");
  Require(mismatched_candidate.Evaluate(mismatch_frame, true, selector) ==
                  SleepImageDecision::kSuppress &&
                  mismatch_calls == 1,
          "geometry mismatch was not terminal for the cycle");

  SleepImageLatch source_validation;
  int source_validation_calls = 0;
  auto source_validation_selector = [&](int, int) {
    ++source_validation_calls;
    return Candidate(sleep);
  };
  Frame null_frame = FullFrame(nullptr);
  Require(source_validation.Evaluate(null_frame, true, source_validation_selector) ==
                  SleepImageDecision::kPassThrough,
          "null source frame did not fail closed");
  Frame undersized = FullFrame(Pixels(2, 2, {0, 0, 0}));
  Require(source_validation.Evaluate(undersized, true, source_validation_selector) ==
                  SleepImageDecision::kPassThrough,
          "actual undersized source did not fail closed");
  Require(source_validation_calls == 0,
          "invalid source extent invoked the selector");
  Frame partial = FullFrame(black);
  partial.dirty_rect.left = 1;
  Require(source_validation.Evaluate(partial, true, selector) ==
                  SleepImageDecision::kPassThrough,
          "non-full geometry did not fail closed");
  Frame invalid = FullFrame(black, 0, 2);
  Require(source_validation.Evaluate(invalid, true, selector) ==
                  SleepImageDecision::kPassThrough,
          "invalid geometry did not fail closed");
  Frame overflow = FullFrame(black, std::numeric_limits<int>::max(),
                             std::numeric_limits<int>::max());
  Require(source_validation.Evaluate(overflow, true, selector) ==
                  SleepImageDecision::kPassThrough,
          "overflow-prone geometry did not fail closed");

  std::cout << "PASS\n";
  return 0;
}
