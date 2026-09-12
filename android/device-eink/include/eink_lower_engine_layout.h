#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace neo2::eink {

// The Musnap X lower engine exposes a 3840x1280 scan plane for its logical
// 1920x2560 panel. Its stock utility rearranger emits each adjacent pair of
// logical rows as alternating bytes in one controller row.
[[nodiscard]] bool InterleaveAdjacentGrayscaleRows(
        const std::uint8_t* source, std::size_t source_bytes, int width, int height,
        std::vector<std::uint8_t>* destination);

}  // namespace neo2::eink
