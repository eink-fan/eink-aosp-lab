#include "eink_lower_engine_layout.h"

#include <limits>

namespace neo2::eink {

bool InterleaveAdjacentGrayscaleRows(const std::uint8_t* source, std::size_t source_bytes,
                                     int width, int height,
                                     std::vector<std::uint8_t>* destination) {
  if (source == nullptr || destination == nullptr || width <= 0 || height <= 0 ||
      (height & 1) != 0) {
    return false;
  }
  const auto size_width = static_cast<std::size_t>(width);
  const auto size_height = static_cast<std::size_t>(height);
  if (size_width > std::numeric_limits<std::size_t>::max() / size_height ||
      source_bytes != size_width * size_height) {
    return false;
  }

  destination->resize(source_bytes);
  for (int source_y = 0; source_y < height; source_y += 2) {
    const auto first_row = static_cast<std::size_t>(source_y) * size_width;
    const auto second_row = first_row + size_width;
    const auto controller_row = static_cast<std::size_t>(source_y / 2) * size_width * 2;
    for (int x = 0; x < width; ++x) {
      const auto source_x = static_cast<std::size_t>(x);
      const auto controller_x = controller_row + source_x * 2;
      (*destination)[controller_x] = source[first_row + source_x];
      (*destination)[controller_x + 1] = source[second_row + source_x];
    }
  }
  return true;
}

}  // namespace neo2::eink
