#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace neo2::eink {

// Owns only the one-shot worker-to-main-thread handoff required by B002's
// full-pool obligation. The platform integration supplies the executor and
// the main-thread action; this class deliberately knows nothing about
// SurfaceFlinger, RenderEngine, queueBuffer, or a panel backend.
class MainThreadResampleDispatcher final {
 public:
  using Task = std::function<void()>;
  // Returns true only when it accepted Task for later main-thread execution.
  // An accepting executor must not run Task before returning to Request().
  using PostTask = std::function<bool(Task)>;
  using MainThreadAction = std::function<void()>;

  enum class RequestResult : std::uint8_t {
    kPosted,
    kCoalesced,
    kRejected,
    kStopped,
  };

  struct Diagnostics {
    std::uint64_t requested = 0;
    std::uint64_t posted = 0;
    std::uint64_t coalesced = 0;
    std::uint64_t rejected = 0;
    std::uint64_t dispatched = 0;
    std::uint64_t cancelled = 0;
    std::uint64_t stopped_requests = 0;
  };

  MainThreadResampleDispatcher(PostTask post_task, MainThreadAction action);
  ~MainThreadResampleDispatcher();

  MainThreadResampleDispatcher(const MainThreadResampleDispatcher&) = delete;
  MainThreadResampleDispatcher& operator=(const MainThreadResampleDispatcher&) = delete;

  // Coalesces while one accepted task is outstanding. A stopped session never
  // posts a later task. The returned task retains state, not this object, so a
  // late executor callback after destruction is safe and becomes a no-op.
  [[nodiscard]] RequestResult Request();
  void Stop();
  [[nodiscard]] Diagnostics diagnostics() const;

 private:
  struct State;

  static void RunPostedTask(const std::shared_ptr<State>& state, std::uint64_t sequence);

  std::shared_ptr<State> state_;
};

}  // namespace neo2::eink
