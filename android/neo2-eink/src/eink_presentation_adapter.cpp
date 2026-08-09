#include "eink_presentation_adapter.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace neo2::eink {

class PresentationAdapter::Impl {
 public:
  std::mutex mutex;
  std::condition_variable condition;
  std::optional<Frame> pending;
  std::thread worker;
  Diagnostics diagnostics;
  std::chrono::steady_clock::time_point last_accepted_submission{};
  bool pending_is_trailing = false;
  bool submitting = false;
  bool started = false;
  bool stopping = false;
};

PresentationAdapter::PresentationAdapter(Engine& engine, SubmissionObserver observer,
                                         SubmissionIntervalProvider interval_provider)
    : engine_(engine),
      submission_observer_(std::move(observer)),
      interval_provider_(std::move(interval_provider)),
      impl_(std::make_unique<Impl>()) {}

PresentationAdapter::~PresentationAdapter() {
  Stop();
}

void PresentationAdapter::Start() {
  std::lock_guard lock(impl_->mutex);
  if (impl_->started) {
    return;
  }
  if (impl_->stopping) {
    // Stop is terminal for this single-owner worker. Android's SurfaceFlinger
    // build disables exceptions, and a silent no-op keeps an accidental
    // post-teardown restart fail-closed rather than reviving a stale engine.
    return;
  }
  impl_->started = true;
  impl_->worker = std::thread(&PresentationAdapter::WorkerMain, this);
}

void PresentationAdapter::Stop() {
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->started || impl_->stopping) {
      return;
    }
    impl_->stopping = true;
    // A pending frame has not reached the panel; discard it during shutdown.
    impl_->pending.reset();
  }
  impl_->condition.notify_one();
  impl_->worker.join();
}

bool PresentationAdapter::Enqueue(Frame frame) {
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->started || impl_->stopping) {
      return false;
    }
    // A vendor backend needs compose_buffer; the capture-only CPU proof owns
    // grayscale staging and intentionally releases its pool-backed compose
    // buffer after conversion. Require at least one understood payload.
    if ((!frame.compose_buffer && !frame.grayscale_buffer) || !frame.dirty_rect.IsValid()) {
      return false;
    }
    // Depth one: replace an older pending frame, but never interrupt Submit.
    if (impl_->pending.has_value()) {
      ++impl_->diagnostics.pending_replaced;
    }
    impl_->pending = std::move(frame);
    ++impl_->diagnostics.enqueues_accepted;
  }
  impl_->condition.notify_one();
  return true;
}

PresentationAdapter::Diagnostics PresentationAdapter::diagnostics() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->diagnostics;
}

PresentationAdapter::OutputSlotState PresentationAdapter::output_slot_state() const {
  std::lock_guard lock(impl_->mutex);
  OutputSlotState state{};
  state.has_retained_candidate = impl_->pending.has_value() || impl_->submitting;
  if (impl_->last_accepted_submission == std::chrono::steady_clock::time_point{}) return state;
  const auto interval = interval_provider_ ? interval_provider_()
                                           : std::chrono::steady_clock::duration::zero();
  if (interval > std::chrono::steady_clock::duration::zero()) {
    state.next_eligible = impl_->last_accepted_submission + interval;
  }
  return state;
}

void PresentationAdapter::WorkerMain() {
  while (true) {
    std::optional<Frame> frame;
    bool trailing_submission = false;
    {
      std::unique_lock lock(impl_->mutex);
      while (!impl_->stopping) {
        impl_->condition.wait(lock, [this] {
          return impl_->stopping || impl_->pending.has_value();
        });
        if (impl_->stopping) return;
        const auto interval = interval_provider_ ? interval_provider_()
                                                 : std::chrono::steady_clock::duration::zero();
        const auto now = std::chrono::steady_clock::now();
        const auto eligible_at = impl_->last_accepted_submission + interval;
        if (impl_->last_accepted_submission != std::chrono::steady_clock::time_point{} &&
            interval > std::chrono::steady_clock::duration::zero() && now < eligible_at) {
          if (!impl_->pending_is_trailing) {
            impl_->pending_is_trailing = true;
            ++impl_->diagnostics.trailing_deferred;
          }
          impl_->condition.wait_until(lock, eligible_at, [this] { return impl_->stopping; });
          continue;
        }
        frame = std::move(impl_->pending);
        impl_->pending.reset();
        impl_->submitting = true;
        trailing_submission = impl_->pending_is_trailing;
        impl_->pending_is_trailing = false;
        break;
      }
      if (impl_->stopping) return;
    }
    // Engine ownership is intentionally serial: it owns waveform/DRM state.
    const auto submit_start = std::chrono::steady_clock::now();
    const bool accepted = engine_.Submit(*frame);
    const auto submit_finish = std::chrono::steady_clock::now();
    {
      std::lock_guard lock(impl_->mutex);
      ++impl_->diagnostics.submit_calls;
      if (!accepted) {
        ++impl_->diagnostics.submit_rejected;
      } else {
        // Start the next eligibility window only after the engine's queue
        // acceptance return. This leaves a small safety margin for a backend
        // that records its own submission timestamp inside Submit().
        impl_->last_accepted_submission = submit_finish;
        if (trailing_submission) ++impl_->diagnostics.trailing_submitted;
      }
      impl_->submitting = false;
    }
    if (submission_observer_) {
      submission_observer_(*frame, accepted, submit_finish - submit_start);
    }
  }
}

}  // namespace neo2::eink
