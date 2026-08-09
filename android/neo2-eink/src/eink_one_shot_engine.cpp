#include "eink_one_shot_engine.h"

namespace neo2::eink {

namespace {

constexpr int kPanelWidth = 1448;
constexpr int kPanelHeight = 1072;
constexpr int kObservedGc16FullMode =
        ToEngineMode(RefreshMode::kGc16Partial) + ToEngineMode(RefreshMode::kFullUpdate);

}  // namespace

OneShotEngine::OneShotEngine(Engine& delegate) : delegate_(delegate) {}

bool OneShotEngine::Arm() {
  std::lock_guard lock(mutex_);
  if (diagnostics_.state != State::kDisarmed) {
    return false;
  }
  diagnostics_.state = State::kArmed;
  return true;
}

bool OneShotEngine::Submit(const Frame& frame) {
  {
    std::lock_guard lock(mutex_);
    if (diagnostics_.state != State::kArmed || !IsObservedOneShotFrame(frame)) {
      ++diagnostics_.rejected;
      return false;
    }
    // Mark terminal before invoking an opaque engine. The vendor wrapper can
    // block or fail; neither outcome is permission to retry in this process.
    diagnostics_.state = State::kAttempted;
    ++diagnostics_.attempts;
  }

  const bool accepted = delegate_.Submit(frame);
  {
    std::lock_guard lock(mutex_);
    diagnostics_.delegate_accepted = accepted;
  }
  return accepted;
}

OneShotEngine::Diagnostics OneShotEngine::diagnostics() const {
  std::lock_guard lock(mutex_);
  return diagnostics_;
}

bool OneShotEngine::IsObservedOneShotFrame(const Frame& frame) {
  // M4 intentionally uses the vendor wrapper's own GraphicBuffer/fence
  // conversion path. Accepting CPU grayscale staging here could accidentally
  // combine two conversion paths or hide an ownership error.
  return frame.compose_buffer != nullptr && frame.source_buffer == nullptr &&
          frame.grayscale_buffer == nullptr && !frame.handwriting && frame.policy_flag == 0 &&
          frame.engine_mode == kObservedGc16FullMode && frame.dirty_rect.left == 0 &&
          frame.dirty_rect.top == 0 && frame.dirty_rect.right == kPanelWidth &&
          frame.dirty_rect.bottom == kPanelHeight;
}

}  // namespace neo2::eink