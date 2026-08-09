#include "eink_grayscale_fidelity.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

einklab::OwnedGrayscaleBuffer Make(int width, int height, std::vector<std::uint8_t> bytes) {
  return einklab::OwnedGrayscaleBuffer(width, height, std::move(bytes));
}

}  // namespace

int main() {
  const auto source = Make(3, 2, {1, 2, 3, 4, 5, 6});
  const auto same = Make(3, 2, {1, 2, 3, 4, 5, 6});
  const auto equal = einklab::CompareGrayscaleFidelity(source, same);
  assert(equal.comparable);
  assert(equal.exact_match);
  assert(equal.differing_pixels == 0);

  const auto changed = Make(3, 2, {1, 9, 3, 4, 5, 8});
  const auto mismatch = einklab::CompareGrayscaleFidelity(source, changed);
  assert(mismatch.comparable);
  assert(!mismatch.exact_match);
  assert(mismatch.differing_pixels == 2);
  assert(mismatch.left == 1 && mismatch.top == 0 && mismatch.right == 3 && mismatch.bottom == 2);

  const auto wrong_size = Make(2, 3, {1, 2, 3, 4, 5, 6});
  const auto incompatible = einklab::CompareGrayscaleFidelity(source, wrong_size);
  assert(!incompatible.comparable);
  assert(!incompatible.exact_match);

  const auto malformed = Make(3, 2, {1, 2, 3, 4, 5});
  const auto malformed_result = einklab::CompareGrayscaleFidelity(source, malformed);
  assert(!malformed_result.comparable);
  return 0;
}
