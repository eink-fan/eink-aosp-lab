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

}  // namespace

SleepImageDecision SleepImageLatch::Evaluate(Frame& frame, bool screen_off_latched,
                                             const Selector& selector) {
  std::size_t frame_size = 0;
  if (!frame.grayscale_buffer || !FullFrameSize(frame, &frame_size) ||
      frame.grayscale_buffer->ByteCount() < frame_size ||
      frame.grayscale_buffer->Data() == nullptr) {
    return SleepImageDecision::kPassThrough;
  }

  const auto* source = frame.grayscale_buffer->Data();
  const bool all_zero = std::all_of(source, source + frame_size,
                                    [](std::uint8_t pixel) { return pixel == 0; });
  if (!all_zero) {
    state_ = State::kIdle;
    return SleepImageDecision::kPassThrough;
  }
  if (state_ != State::kIdle) {
    return SleepImageDecision::kSuppress;
  }
  if (!screen_off_latched) {
    return SleepImageDecision::kPassThrough;
  }
  state_ = State::kFailed;
  if (!selector) {
    return SleepImageDecision::kPassThrough;
  }

  SleepImageCandidate candidate = selector(frame.dirty_rect.right, frame.dirty_rect.bottom);
  if (!candidate.buffer || candidate.width != frame.dirty_rect.right ||
      candidate.height != frame.dirty_rect.bottom ||
      candidate.buffer->ByteCount() < frame_size ||
      candidate.buffer->Data() == nullptr) {
    return SleepImageDecision::kPassThrough;
  }

  frame.grayscale_buffer = std::move(candidate.buffer);
  state_ = State::kSelected;
  return SleepImageDecision::kReplace;
}

}  // namespace neo2::eink
