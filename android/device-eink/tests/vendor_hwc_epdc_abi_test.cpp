#include "vendor_hwc_epdc_abi.h"

#include <cassert>

int main() {
  // The exact record observed on the stock Neo 2's rotated 1448x1072 panel.
  const auto record = neo2::eink::MakeVendorFullFrameRecord(1448, 1072, 0x22);
  assert(record.left == 0);
  assert(record.top == 0);
  assert(record.right == 1448);
  assert(record.bottom == 1072);
  assert(record.mode_bits == 0x22);
  assert(record.unknown0 == 0);
  assert(record.next_node == 0);
}
