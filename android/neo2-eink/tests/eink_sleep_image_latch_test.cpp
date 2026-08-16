#include "eink_sleep_image_latch.h"
#include "eink_software_grayscale.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {

using neo2::eink::Frame;
using neo2::eink::OwnedGrayscaleBuffer;
using neo2::eink::SleepImageCandidate;
using neo2::eink::SleepImageDecision;
using neo2::eink::SleepImageArmResult;
using neo2::eink::SleepImageDisarmResult;
using neo2::eink::SleepImageLatch;
using neo2::eink::SleepImagePresentationResult;
using neo2::eink::TerminalBlackOutcome;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::shared_ptr<OwnedGrayscaleBuffer> Pixels(std::vector<std::uint8_t> pixels) {
  return std::make_shared<OwnedGrayscaleBuffer>(2, 2, std::move(pixels));
}

Frame FullFrame(const std::shared_ptr<OwnedGrayscaleBuffer>& pixels) {
  return Frame{
          .compose_buffer = nullptr,
          .source_buffer = nullptr,
          .grayscale_buffer = pixels,
          .dirty_rect = {.left = 0, .top = 0, .right = 2, .bottom = 2},
  };
}

SleepImageCandidate Candidate(const std::shared_ptr<OwnedGrayscaleBuffer>& pixels) {
  return {.buffer = pixels, .alpha = {}, .width = 2, .height = 2};
}

}  // namespace

int main() {
  const auto terminal_black = Pixels({0, 96, 3, 40});
  const auto non_terminal = Pixels({0, 0, 97, 0});
  const auto sleep = Pixels({0x10, 0x20, 0x30, 0x40});

  SleepImageLatch latch;
  int selector_calls = 0;
  auto selector = [&](int width, int height) {
    ++selector_calls;
    Require(width == 2 && height == 2, "selector received wrong geometry");
    return Candidate(sleep);
  };

  Frame inactive_black = FullFrame(terminal_black);
  Require(latch.Evaluate(inactive_black, selector) == SleepImageDecision::kPassThrough &&
                  inactive_black.grayscale_buffer == terminal_black && selector_calls == 0,
          "inactive latch selected a sleep image");
  const auto inactive_summary = latch.last_evaluation();
  Require(inactive_summary.valid_full_frame && inactive_summary.minimum_gray == 0 &&
                  inactive_summary.maximum_gray == 96 && inactive_summary.mean_gray == 34 &&
                  inactive_summary.threshold == 96 && !inactive_summary.ready &&
                  !inactive_summary.holding &&
                  inactive_summary.decision == SleepImageDecision::kPassThrough,
          "inactive maximum-96 evaluation summary was not recorded");

  Require(latch.ArmSleepCycle(0) == SleepImageArmResult::kRejectedZero,
          "zero epoch was armed");
  Require(latch.ArmSleepCycle(7) == SleepImageArmResult::kAccepted,
          "first lifecycle epoch was not accepted");
  Require(latch.ArmSleepCycle(7) == SleepImageArmResult::kRejectedDuplicate,
          "duplicate lifecycle epoch was accepted");
  Require(latch.ArmSleepCycle(6) == SleepImageArmResult::kRejectedStale,
          "stale lifecycle epoch was accepted");
  Require(latch.ArmSleepCycle(8) == SleepImageArmResult::kRejectedMismatched,
          "a second active lifecycle epoch was accepted");

  Frame first = FullFrame(terminal_black);
  Require(latch.Evaluate(first, selector) == SleepImageDecision::kReplace,
          "first armed terminal-black frame was not replaced");
  Require(first.grayscale_buffer == sleep && latch.selection_latched() && selector_calls == 1,
          "replacement did not consume exactly one armed sleep cycle");
  Require(latch.last_outcome() == TerminalBlackOutcome::kCandidateReplaced,
          "replacement outcome was not recorded");
  const auto first_summary = latch.last_evaluation();
  Require(first_summary.ready && !first_summary.holding &&
                  first_summary.decision == SleepImageDecision::kReplace,
          "armed replacement summary was not recorded");

  Frame duplicate = FullFrame(terminal_black);
  Require(latch.Evaluate(duplicate, selector) == SleepImageDecision::kSuppress && selector_calls == 1,
          "duplicate black was not held after replacement");
  Require(latch.last_outcome() == TerminalBlackOutcome::kDuplicateSuppressed,
          "duplicate suppression outcome was not recorded");
  const auto duplicate_summary = latch.last_evaluation();
  Require(!duplicate_summary.ready && duplicate_summary.holding &&
                  duplicate_summary.maximum_gray == 96 &&
                  duplicate_summary.decision == SleepImageDecision::kSuppress,
          "holding-state summary was not recorded for a darker duplicate");
  Require(latch.DisarmSleepCycle(8) == SleepImageDisarmResult::kRejectedMismatched,
          "mismatched diagnostic epoch was disarmed");
  Require(latch.DisarmSleepCycle(7) == SleepImageDisarmResult::kAccepted,
          "matching lifecycle epoch was not disarmed");
  Frame after_disarm = FullFrame(terminal_black);
  Require(latch.Evaluate(after_disarm, selector) == SleepImageDecision::kPassThrough &&
                  after_disarm.grayscale_buffer == terminal_black &&
                  selector_calls == 1,
          "matching disarm left functional eligibility active");

  Frame non_terminal_frame = FullFrame(non_terminal);
  Require(latch.Evaluate(non_terminal_frame, selector) == SleepImageDecision::kPassThrough,
          "non-terminal full frame did not pass through");
  Require(!latch.selection_latched() && latch.last_outcome() == TerminalBlackOutcome::kNone,
          "ordinary content rearmed an inactive cycle");
  Frame next = FullFrame(terminal_black);
  Require(latch.Evaluate(next, selector) == SleepImageDecision::kPassThrough && selector_calls == 1,
          "ordinary content armed the next terminal-black frame");

  Require(latch.ArmSleepCycle(8) == SleepImageArmResult::kAccepted,
          "newer lifecycle epoch was not accepted after disarm");
  Require(latch.BeginPresentation(0) == SleepImagePresentationResult::kRejectedZero,
          "zero direct-presentation epoch was accepted");
  Require(latch.BeginPresentation(9) == SleepImagePresentationResult::kRejectedMismatched,
          "mismatched direct-presentation epoch was accepted");
  Require(latch.BeginPresentation(8) == SleepImagePresentationResult::kAccepted,
          "matching direct-presentation epoch was not consumed");
  Require(latch.BeginPresentation(8) == SleepImagePresentationResult::kRejectedDuplicate,
          "duplicate direct-presentation epoch was accepted");
  Frame second_cycle = FullFrame(non_terminal);
  Require(latch.Evaluate(second_cycle, selector) == SleepImageDecision::kPassThrough &&
                  selector_calls == 1,
          "direct presentation changed ordinary frame pixels");
  Require(latch.DisarmSleepCycle(8) == SleepImageDisarmResult::kAccepted,
          "second lifecycle cycle did not disarm");
  Require(latch.BeginPresentation(8) == SleepImagePresentationResult::kRejectedInactive,
          "inactive direct-presentation epoch was accepted");

  SleepImageLatch missing_catalog;
  Require(missing_catalog.ArmSleepCycle(9) == SleepImageArmResult::kAccepted,
          "missing-catalog cycle did not arm");
  Frame missing = FullFrame(terminal_black);
  Require(missing_catalog.Evaluate(missing, {}) == SleepImageDecision::kPassThrough &&
                  missing.grayscale_buffer == terminal_black,
          "missing catalog did not pass ordinary black through");
  Require(missing_catalog.last_outcome() == TerminalBlackOutcome::kCatalogMissingFallback,
          "catalog-missing fallback was not recorded");
  int fallback_selector_calls = 0;
  auto fallback_selector = [&](int, int) {
    ++fallback_selector_calls;
    return Candidate(sleep);
  };
  Frame continuing_fallback = FullFrame(terminal_black);
  Require(missing_catalog.Evaluate(continuing_fallback, fallback_selector) ==
                  SleepImageDecision::kPassThrough &&
                  continuing_fallback.grayscale_buffer == terminal_black &&
                  fallback_selector_calls == 0,
          "black fallback run retried selection or retained earlier content");

  Frame fallback_end = FullFrame(non_terminal);
  Require(missing_catalog.Evaluate(fallback_end, fallback_selector) ==
                  SleepImageDecision::kPassThrough &&
                  missing_catalog.last_outcome() == TerminalBlackOutcome::kNone,
          "non-terminal frame changed the lifecycle-bound fallback");
  Frame after_fallback = FullFrame(terminal_black);
  Require(missing_catalog.Evaluate(after_fallback, fallback_selector) ==
                  SleepImageDecision::kPassThrough &&
                  fallback_selector_calls == 0,
          "ordinary content retried selection in a fallback cycle");
  Require(missing_catalog.DisarmSleepCycle(9) == SleepImageDisarmResult::kAccepted,
          "fallback cycle did not disarm");

  SleepImageLatch invalid_candidate;
  Require(invalid_candidate.ArmSleepCycle(10) == SleepImageArmResult::kAccepted,
          "invalid-candidate cycle did not arm");
  int invalid_selector_calls = 0;
  Frame invalid_candidate_black = FullFrame(terminal_black);
  Require(invalid_candidate.Evaluate(
                  invalid_candidate_black,
                  [&](int, int) {
                    ++invalid_selector_calls;
                    return SleepImageCandidate{.buffer = sleep, .alpha = {}, .width = 1, .height = 2};
                  }) == SleepImageDecision::kPassThrough &&
                  invalid_candidate_black.grayscale_buffer == terminal_black &&
                  invalid_selector_calls == 1,
          "invalid candidate did not enter safe black fallback");
  Frame invalid_candidate_repeat = FullFrame(terminal_black);
  Require(invalid_candidate.Evaluate(invalid_candidate_repeat, fallback_selector) ==
                  SleepImageDecision::kPassThrough &&
                  fallback_selector_calls == 0,
          "invalid-candidate fallback retried within the same black run");

  SleepImageLatch malformed_input;
  Require(malformed_input.ArmSleepCycle(11) == SleepImageArmResult::kAccepted,
          "malformed-input cycle did not arm");
  Frame invalid = FullFrame(Pixels({0, 0, 0}));
  Require(malformed_input.Evaluate(invalid, selector) == SleepImageDecision::kPassThrough,
          "undersized frame did not pass through");
  Require(malformed_input.last_outcome() == TerminalBlackOutcome::kNone,
          "invalid frame consumed or changed the ready policy");
  Require(!malformed_input.last_evaluation().valid_full_frame,
          "invalid frame produced a valid-frame summary");
  Frame partial = FullFrame(terminal_black);
  partial.dirty_rect.left = 1;
  Require(malformed_input.Evaluate(partial, selector) == SleepImageDecision::kPassThrough &&
                  selector_calls == 1,
          "partial frame consumed the ready policy");
  Frame after_invalid = FullFrame(terminal_black);
  Require(malformed_input.Evaluate(after_invalid, selector) == SleepImageDecision::kReplace &&
                  selector_calls == 2,
          "invalid frame consumed the next valid black opportunity");

  Require(malformed_input.DisarmSleepCycle(11) == SleepImageDisarmResult::kAccepted,
          "malformed-input cycle did not disarm");
  Require(malformed_input.DisarmSleepCycle(11) == SleepImageDisarmResult::kRejectedInactive,
          "inactive cycle accepted a second disarm");

  std::cout << "PASS\n";
  return 0;
}
