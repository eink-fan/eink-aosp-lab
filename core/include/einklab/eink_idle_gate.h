#pragma once

#include <cstdint>
#include <functional>

namespace einklab {

// Bounded, submission-order-only readiness gate. A zero query result is
// ready; any nonzero result is busy. Callers inject waiting so deterministic
// tests never sleep and platform retains the exact one-millisecond cadence.
class IdleGate final {
 public:
  struct Result {
    bool ready = false;
    std::uint32_t busy_retries = 0;
  };

  struct Diagnostics {
    std::uint64_t queries = 0;
    std::uint64_t busy_retries = 0;
    std::uint64_t ready = 0;
    std::uint64_t timeouts = 0;
  };

  using Query = std::function<int()>;
  using WaitOneInterval = std::function<void()>;

  // `maximum_busy_retries` bounds waits, not query calls. A value of 2,000
  // performs at most 2,001 queries and at most 2,000 one-millisecond waits.
  [[nodiscard]] Result WaitUntilReady(const Query& query, const WaitOneInterval& wait_one_interval,
                                      std::uint32_t maximum_busy_retries);
  [[nodiscard]] Diagnostics diagnostics() const { return diagnostics_; }

 private:
  Diagnostics diagnostics_;
};

}  // namespace einklab
