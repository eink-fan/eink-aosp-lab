#include "eink_idle_gate.h"

#include <cassert>
#include <cstdint>

int main() {
  einklab::IdleGate gate;
  std::uint32_t queries = 0;
  std::uint32_t waits = 0;
  const auto immediate = gate.WaitUntilReady([&] { ++queries; return 0; }, [&] { ++waits; }, 2'000);
  assert(immediate.ready && immediate.busy_retries == 0 && queries == 1 && waits == 0);

  queries = 0;
  waits = 0;
  const auto delayed = gate.WaitUntilReady(
          [&] { return ++queries == 3 ? 0 : 1; }, [&] { ++waits; }, 2'000);
  assert(delayed.ready && delayed.busy_retries == 2 && queries == 3 && waits == 2);

  queries = 0;
  waits = 0;
  const auto timeout = gate.WaitUntilReady(
          [&] { ++queries; return 1; }, [&] { ++waits; }, 3);
  assert(!timeout.ready && timeout.busy_retries == 3 && queries == 4 && waits == 3);
  const auto diagnostics = gate.diagnostics();
  assert(diagnostics.queries == 8 && diagnostics.busy_retries == 5 && diagnostics.ready == 2 &&
         diagnostics.timeouts == 1);
  return 0;
}
