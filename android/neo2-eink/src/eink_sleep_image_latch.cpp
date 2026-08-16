#include "eink_sleep_image_latch.h"

#include <algorithm>
#include <limits>

namespace neo2::eink {

namespace {

bool FullFrameSize(const Frame& frame, std::size_t* size) {
  if (size == nullptr || frame.dirty_rect.left != 0 || frame.dirty_rect.top != 0 ||
      !frame.dirty_rect.IsValid()) {
    return false;
  }
  if (frame.dirty_rect.right >
      std::numeric_limits<int>::max() / frame.dirty_rect.bottom) {
    return false;
  }
  const auto width = static_cast<std::size_t>(frame.dirty_rect.right);
  const auto height = static_cast<std::size_t>(frame.dirty_rect.bottom);
  if (width > std::numeric_limits<std::size_t>::max() / height) {
    return false;
  }
  *size = width * height;
  return true;
}

SleepImageEvaluationSummary Summarize(const GrayscaleBuffer& buffer, std::size_t size,
                                      SleepImageLatchState state_before) {
  SleepImageEvaluationSummary summary{
          .valid_full_frame = true,
          .minimum_gray = std::numeric_limits<std::uint8_t>::max(),
          .maximum_gray = 0,
          .mean_gray = 0,
          .threshold = kSleepImageMaximumGray,
          .state_before = state_before,
          .ready = state_before == SleepImageLatchState::kArmed,
          .holding = state_before == SleepImageLatchState::kConsumed,
  };
  const std::uint8_t* const bytes = buffer.Data();
  std::uint64_t sum = 0;
  for (std::size_t index = 0; index < size; ++index) {
    summary.minimum_gray = std::min(summary.minimum_gray, bytes[index]);
    summary.maximum_gray = std::max(summary.maximum_gray, bytes[index]);
    sum += bytes[index];
  }
  summary.mean_gray = static_cast<std::uint8_t>(sum / size);
  return summary;
}

}  // namespace

SleepImageArmResult SleepImageLatch::ArmSleepCycle(std::uint64_t epoch) {
  if (epoch == 0) return SleepImageArmResult::kRejectedZero;
  if (epoch < last_armed_epoch_) return SleepImageArmResult::kRejectedStale;
  if (epoch == last_armed_epoch_) return SleepImageArmResult::kRejectedDuplicate;
  if (active_epoch_ != 0) return SleepImageArmResult::kRejectedMismatched;
  last_armed_epoch_ = epoch;
  active_epoch_ = epoch;
  state_ = SleepImageLatchState::kArmed;
  return SleepImageArmResult::kAccepted;
}

SleepImagePresentationResult SleepImageLatch::BeginPresentation(std::uint64_t epoch) {
  if (epoch == 0) return SleepImagePresentationResult::kRejectedZero;
  if (active_epoch_ == 0) return SleepImagePresentationResult::kRejectedInactive;
  if (epoch != active_epoch_) return SleepImagePresentationResult::kRejectedMismatched;
  if (state_ != SleepImageLatchState::kArmed) {
    return SleepImagePresentationResult::kRejectedDuplicate;
  }
  state_ = SleepImageLatchState::kConsumed;
  return SleepImagePresentationResult::kAccepted;
}

SleepImageDisarmResult SleepImageLatch::DisarmSleepCycle(std::uint64_t epoch) {
  if (epoch == 0) return SleepImageDisarmResult::kRejectedZero;
  if (active_epoch_ == 0) return SleepImageDisarmResult::kRejectedInactive;
  if (epoch != active_epoch_) return SleepImageDisarmResult::kRejectedMismatched;
  active_epoch_ = 0;
  state_ = SleepImageLatchState::kInactive;
  return SleepImageDisarmResult::kAccepted;
}

SleepImageDecision SleepImageLatch::Evaluate(Frame& frame, const Selector& selector) {
  last_outcome_ = TerminalBlackOutcome::kNone;
  last_evaluation_ = {};
  std::size_t frame_size = 0;
  if (!frame.grayscale_buffer || !FullFrameSize(frame, &frame_size) ||
      frame.grayscale_buffer->ByteCount() != frame_size ||
      frame.grayscale_buffer->Data() == nullptr) {
    return SleepImageDecision::kPassThrough;
  }

  last_evaluation_ = Summarize(*frame.grayscale_buffer, frame_size, state_);
  const auto finish = [this](SleepImageDecision decision) {
    last_evaluation_.decision = decision;
    last_evaluation_.outcome = last_outcome_;
    return decision;
  };

  if (last_evaluation_.maximum_gray > kSleepImageMaximumGray) {
    return finish(SleepImageDecision::kPassThrough);
  }

  if (state_ == SleepImageLatchState::kInactive) {
    return finish(SleepImageDecision::kPassThrough);
  }

  if (state_ == SleepImageLatchState::kConsumed) {
    last_outcome_ = TerminalBlackOutcome::kDuplicateSuppressed;
    return finish(SleepImageDecision::kSuppress);
  }
  if (state_ == SleepImageLatchState::kBlackFallback) {
    last_outcome_ = TerminalBlackOutcome::kCatalogMissingFallback;
    return finish(SleepImageDecision::kPassThrough);
  }

  if (!selector) {
    state_ = SleepImageLatchState::kBlackFallback;
    last_outcome_ = TerminalBlackOutcome::kCatalogMissingFallback;
    return finish(SleepImageDecision::kPassThrough);
  }

  SleepImageCandidate candidate = selector(frame.dirty_rect.right, frame.dirty_rect.bottom);
  if (!candidate.buffer || candidate.width != frame.dirty_rect.right ||
      candidate.height != frame.dirty_rect.bottom ||
      candidate.buffer->ByteCount() != frame_size ||
      candidate.buffer->Data() == nullptr) {
    state_ = SleepImageLatchState::kBlackFallback;
    last_outcome_ = TerminalBlackOutcome::kCatalogMissingFallback;
    return finish(SleepImageDecision::kPassThrough);
  }

  state_ = SleepImageLatchState::kConsumed;
  frame.grayscale_buffer = std::move(candidate.buffer);
  last_outcome_ = TerminalBlackOutcome::kCandidateReplaced;
  return finish(SleepImageDecision::kReplace);
}

}  // namespace neo2::eink
