#include "eink_scalar_strncpy.h"

#include <array>
#include <cassert>
#include <cstddef>

int main() {
  using neo2::eink::ScalarStrncpy;

  std::array<char, 8> destination{'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
  const char source[] = "abc";
  assert(ScalarStrncpy(destination.data(), source, destination.size()) == destination.data());
  assert(destination[0] == 'a');
  assert(destination[1] == 'b');
  assert(destination[2] == 'c');
  for (std::size_t index = 3; index < destination.size(); ++index) {
    assert(destination[index] == '\0');
  }

  destination.fill('x');
  assert(ScalarStrncpy(destination.data(), "abcdefghij", 4) == destination.data());
  assert(destination[0] == 'a');
  assert(destination[1] == 'b');
  assert(destination[2] == 'c');
  assert(destination[3] == 'd');
  assert(destination[4] == 'x');

  destination.fill('x');
  assert(ScalarStrncpy(destination.data(), source, 0) == destination.data());
  for (char value : destination) assert(value == 'x');
}
