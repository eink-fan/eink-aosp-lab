#pragma once

#include <cstddef>
#include <cstdint>

namespace neo2::eink {

// Observed ABI of the stock `hwc_epdc_llist` record at the
// RefreshThread::requestRefresh boundary. This is intentionally *not* a full
// semantic declaration: byte 0x14 remains unknown, and bytes 0x18..0x1f are
// a producer-owned linked-list pointer. Both are zero in all 103 captured
// full-frame submissions. Partial-update behavior remains separately
// unrecovered.
struct VendorHwcEpdcFullFrameRecord {
  std::int32_t left;
  std::int32_t top;
  std::int32_t right;
  std::int32_t bottom;
  std::uint32_t mode_bits;
  std::uint32_t unknown0;
  std::uintptr_t next_node;
};

static_assert(sizeof(VendorHwcEpdcFullFrameRecord) == 0x20);
static_assert(offsetof(VendorHwcEpdcFullFrameRecord, mode_bits) == 0x10);
static_assert(offsetof(VendorHwcEpdcFullFrameRecord, unknown0) == 0x14);
static_assert(offsetof(VendorHwcEpdcFullFrameRecord, next_node) == 0x18);

// Creates only the full-panel record observed on stock. A future partial
// update path must not reuse this helper until the unknown word and list-node
// ownership rules have been captured and verified.
[[nodiscard]] constexpr VendorHwcEpdcFullFrameRecord MakeVendorFullFrameRecord(
        std::int32_t panel_width, std::int32_t panel_height,
        std::uint32_t mode_bits) {
  return {
          .left = 0,
          .top = 0,
          .right = panel_width,
          .bottom = panel_height,
          .mode_bits = mode_bits,
          .unknown0 = 0,
          .next_node = 0,
  };
}

}  // namespace neo2::eink
