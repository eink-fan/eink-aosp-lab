#include "neo2/eink/eink_sleep_image_catalog.h"

#include <cstdlib>
#include <iostream>

namespace {

using neo2::eink::android::EinkSleepImageCatalog;

void Require(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

}  // namespace

int main() {
  const std::size_t exact = EinkSleepImageCatalog::kPanelBytes;
  Require(EinkSleepImageCatalog::IsPanelByteSize(exact),
          "the exact panel allocation must be accepted");
  Require(!EinkSleepImageCatalog::IsPanelByteSize(0),
          "a zero-size shared-memory descriptor must be rejected");
  Require(!EinkSleepImageCatalog::IsPanelByteSize(exact - 1),
          "a one-byte-short shared-memory descriptor must be rejected");
  Require(!EinkSleepImageCatalog::IsPanelByteSize(exact + 1),
          "a one-byte-long shared-memory descriptor must be rejected");
  const std::size_t ocean_exact =
          EinkSleepImageCatalog::PanelBytesFor(neo2::eink::kOceanDeviceProfile);
  Require(EinkSleepImageCatalog::IsPanelByteSize(
                  ocean_exact, neo2::eink::kOceanDeviceProfile),
          "the exact Ocean panel allocation must be accepted");
  Require(!EinkSleepImageCatalog::IsPanelByteSize(
                  exact, neo2::eink::kOceanDeviceProfile),
          "the Neo allocation must not be accepted for Ocean");
  return 0;
}
