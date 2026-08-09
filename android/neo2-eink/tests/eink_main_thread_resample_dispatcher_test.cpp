#include "eink_main_thread_resample_dispatcher.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {

using neo2::eink::MainThreadResampleDispatcher;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

class QueuedExecutor final {
 public:
  [[nodiscard]] bool Post(MainThreadResampleDispatcher::Task task) {
    tasks_.push_back(std::move(task));
    return true;
  }

  void RunOne() {
    Require(!tasks_.empty(), "executor had no queued task");
    auto task = std::move(tasks_.front());
    tasks_.erase(tasks_.begin());
    task();
  }

  [[nodiscard]] size_t size() const { return tasks_.size(); }

 private:
  std::vector<MainThreadResampleDispatcher::Task> tasks_;
};

}  // namespace

int main() {
  QueuedExecutor executor;
  int actions = 0;
  MainThreadResampleDispatcher dispatcher(
          [&executor](MainThreadResampleDispatcher::Task task) { return executor.Post(std::move(task)); },
          [&actions] { ++actions; });

  Require(dispatcher.Request() == MainThreadResampleDispatcher::RequestResult::kPosted,
          "first request was not posted");
  Require(dispatcher.Request() == MainThreadResampleDispatcher::RequestResult::kCoalesced,
          "second outstanding request was not coalesced");
  Require(executor.size() == 1, "coalesced request added another main-thread task");
  executor.RunOne();
  Require(actions == 1, "accepted task did not invoke the main-thread action once");
  const auto accepted = dispatcher.diagnostics();
  Require(accepted.requested == 2 && accepted.posted == 1 && accepted.coalesced == 1 &&
                  accepted.dispatched == 1 && accepted.rejected == 0 && accepted.cancelled == 0,
          "accepted/coalesced dispatcher diagnostics did not reconcile");

  MainThreadResampleDispatcher rejected(
          [](MainThreadResampleDispatcher::Task) { return false; }, [] {});
  Require(rejected.Request() == MainThreadResampleDispatcher::RequestResult::kRejected,
          "rejected executor request was accepted");
  const auto rejected_diagnostics = rejected.diagnostics();
  Require(rejected_diagnostics.requested == 1 && rejected_diagnostics.rejected == 1 &&
                  rejected_diagnostics.posted == 0 && rejected_diagnostics.dispatched == 0,
          "rejected dispatcher diagnostics did not reconcile");

  QueuedExecutor cancelled_executor;
  int cancelled_actions = 0;
  MainThreadResampleDispatcher cancelled(
          [&cancelled_executor](MainThreadResampleDispatcher::Task task) {
            return cancelled_executor.Post(std::move(task));
          },
          [&cancelled_actions] { ++cancelled_actions; });
  Require(cancelled.Request() == MainThreadResampleDispatcher::RequestResult::kPosted,
          "cancellation test did not post a task");
  cancelled.Stop();
  cancelled_executor.RunOne();
  Require(cancelled_actions == 0, "cancelled task reached the main-thread action");
  Require(cancelled.Request() == MainThreadResampleDispatcher::RequestResult::kStopped,
          "stopped dispatcher accepted a new request");
  const auto cancelled_diagnostics = cancelled.diagnostics();
  Require(cancelled_diagnostics.cancelled == 1 && cancelled_diagnostics.stopped_requests == 1 &&
                  cancelled_diagnostics.dispatched == 0,
          "cancelled dispatcher diagnostics did not reconcile");

  QueuedExecutor teardown_executor;
  int teardown_actions = 0;
  {
    auto teardown = std::make_unique<MainThreadResampleDispatcher>(
            [&teardown_executor](MainThreadResampleDispatcher::Task task) {
              return teardown_executor.Post(std::move(task));
            },
            [&teardown_actions] { ++teardown_actions; });
    Require(teardown->Request() == MainThreadResampleDispatcher::RequestResult::kPosted,
            "teardown test did not post a task");
  }
  teardown_executor.RunOne();
  Require(teardown_actions == 0, "late task dereferenced a destroyed dispatcher session");

  std::cout << "PASS\n";
  return 0;
}
