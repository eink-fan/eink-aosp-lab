#include "eink_idle_gate.h"

namespace neo2::eink {

IdleGate::Result IdleGate::WaitUntilReady(const Query& query, const WaitOneInterval& wait_one_interval,
                                          std::uint32_t maximum_busy_retries) {
  Result result;
  if (!query) {
    ++diagnostics_.timeouts;
    return result;
  }
  while (true) {
    ++diagnostics_.queries;
    if (query() == 0) {
      result.ready = true;
      ++diagnostics_.ready;
      return result;
    }
    if (result.busy_retries >= maximum_busy_retries) {
      ++diagnostics_.timeouts;
      return result;
    }
    ++result.busy_retries;
    ++diagnostics_.busy_retries;
    if (wait_one_interval) wait_one_interval();
  }
}

}  // namespace neo2::eink
