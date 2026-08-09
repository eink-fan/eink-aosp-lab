#include "eink_grayscale_fidelity.h"

#include <algorithm>
#include <limits>

namespace neo2::eink {

GrayscaleFidelityResult CompareGrayscaleFidelity(const OwnedGrayscaleBuffer& source,
                                                 const OwnedGrayscaleBuffer& snapshot) {
  GrayscaleFidelityResult result;
  if (source.Width() <= 0 || source.Height() <= 0 || source.Width() != snapshot.Width() ||
      source.Height() != snapshot.Height() || source.Size() != snapshot.Size()) {
    return result;
  }
  const auto width = static_cast<std::uint64_t>(source.Width());
  const auto height = static_cast<std::uint64_t>(source.Height());
  if (width > std::numeric_limits<std::size_t>::max() / height ||
      source.Size() != static_cast<std::size_t>(width * height)) {
    return result;
  }

  result.comparable = true;
  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = -1;
  int max_y = -1;
  for (int y = 0; y < source.Height(); ++y) {
    for (int x = 0; x < source.Width(); ++x) {
      const auto offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(source.Width()) +
              static_cast<std::size_t>(x);
      if (source.Data()[offset] == snapshot.Data()[offset]) continue;
      ++result.differing_pixels;
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }
  result.exact_match = result.differing_pixels == 0;
  if (!result.exact_match) {
    result.left = min_x;
    result.top = min_y;
    result.right = max_x + 1;
    result.bottom = max_y + 1;
  }
  return result;
}

}  // namespace neo2::eink
