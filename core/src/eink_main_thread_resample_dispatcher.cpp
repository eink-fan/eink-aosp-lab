#include "eink_main_thread_resample_dispatcher.h"

#include <utility>

namespace einklab {

struct MainThreadResampleDispatcher::State {
  mutable std::mutex mutex;
  PostTask post_task;
  MainThreadAction action;
  Diagnostics diagnostics;
  bool stopped = false;
  bool outstanding = false;
  std::uint64_t sequence = 0;
};

MainThreadResampleDispatcher::MainThreadResampleDispatcher(PostTask post_task,
                                                           MainThreadAction action)
    : state_(std::make_shared<State>()) {
  state_->post_task = std::move(post_task);
  state_->action = std::move(action);
}

MainThreadResampleDispatcher::~MainThreadResampleDispatcher() {
  Stop();
}

MainThreadResampleDispatcher::RequestResult MainThreadResampleDispatcher::Request() {
  const auto state = state_;
  std::uint64_t sequence = 0;
  {
    std::lock_guard lock(state->mutex);
    ++state->diagnostics.requested;
    if (state->stopped) {
      ++state->diagnostics.stopped_requests;
      return RequestResult::kStopped;
    }
    if (state->outstanding) {
      ++state->diagnostics.coalesced;
      return RequestResult::kCoalesced;
    }
    if (!state->post_task) {
      ++state->diagnostics.rejected;
      return RequestResult::kRejected;
    }
    state->outstanding = true;
    sequence = ++state->sequence;
  }

  const bool accepted = state->post_task([state, sequence] { RunPostedTask(state, sequence); });
  if (!accepted) {
    std::lock_guard lock(state->mutex);
    if (state->sequence == sequence && state->outstanding) {
      state->outstanding = false;
      ++state->diagnostics.rejected;
    }
    return RequestResult::kRejected;
  }

  {
    std::lock_guard lock(state->mutex);
    ++state->diagnostics.posted;
  }
  return RequestResult::kPosted;
}

void MainThreadResampleDispatcher::Stop() {
  const auto state = state_;
  std::lock_guard lock(state->mutex);
  if (state->stopped) return;
  state->stopped = true;
  if (state->outstanding) {
    state->outstanding = false;
    ++state->diagnostics.cancelled;
  }
}

MainThreadResampleDispatcher::Diagnostics MainThreadResampleDispatcher::diagnostics() const {
  const auto state = state_;
  std::lock_guard lock(state->mutex);
  return state->diagnostics;
}

void MainThreadResampleDispatcher::RunPostedTask(const std::shared_ptr<State>& state,
                                                 std::uint64_t sequence) {
  MainThreadAction action;
  {
    std::lock_guard lock(state->mutex);
    if (state->stopped || !state->outstanding || state->sequence != sequence) return;
    state->outstanding = false;
    ++state->diagnostics.dispatched;
    action = state->action;
  }
  if (action) action();
}

}  // namespace einklab
