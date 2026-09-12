#include "eink_debug_property.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using neo2::eink::ParseBoundedDebugInt;

  const auto absent = ParseBoundedDebugInt("", 300, 100, 1'000);
  Require(absent.value == 300 && absent.used_default && !absent.invalid,
          "an absent property did not use its default");
  const auto exact = ParseBoundedDebugInt("1000", 300, 100, 1'000);
  Require(exact.value == 1'000 && !exact.used_default && !exact.invalid,
          "a boundary value was rejected");
  const auto malformed = ParseBoundedDebugInt("three-hundred", 300, 100, 1'000);
  Require(malformed.value == 300 && malformed.used_default && malformed.invalid,
          "a malformed property did not fail closed to default");
  const auto low = ParseBoundedDebugInt("99", 300, 100, 1'000);
  const auto high = ParseBoundedDebugInt("1001", 300, 100, 1'000);
  Require(low.invalid && high.invalid, "out-of-range properties were accepted");
  const auto invalid_contract = ParseBoundedDebugInt("300", 300, 1'000, 100);
  Require(invalid_contract.invalid && invalid_contract.used_default,
          "an invalid property contract was not rejected");

  // B001's volatile controls each have an independently locked fallback and
  // range. Exercise them here so a later refactor cannot silently widen a
  // debug setting into an unbounded runtime policy.
  const auto changed = ParseBoundedDebugInt("", 2'000, 100, 10'000);
  const auto bbox = ParseBoundedDebugInt("", 5'000, 100, 25'000);
  const auto edge = ParseBoundedDebugInt("", 120'000, 10'000, 250'000);
  const auto settle = ParseBoundedDebugInt("", 300, 100, 1'000);
  const auto repeat = ParseBoundedDebugInt("", 2'000, 500, 5'000);
  const auto late = ParseBoundedDebugInt("", 100, 25, 250);
  const auto poll = ParseBoundedDebugInt("", 1'000, 250, 5'000);
  Require(changed.value == 2'000 && bbox.value == 5'000 && edge.value == 120'000 &&
                  settle.value == 300 && repeat.value == 2'000 && late.value == 100 &&
                  poll.value == 1'000,
          "a B001 volatile-control default changed");
  Require(ParseBoundedDebugInt("10001", 2'000, 100, 10'000).invalid &&
                  ParseBoundedDebugInt("24", 100, 25, 250).invalid &&
                  ParseBoundedDebugInt("5001", 1'000, 250, 5'000).invalid,
          "a B001 volatile-control clamp changed");
  std::cout << "PASS\n";
  return 0;
}
