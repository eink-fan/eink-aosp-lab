#include "eink_device_profile.h"

#include <cassert>

int main() {
  using namespace neo2::eink;

  static_assert(kNeo2DeviceProfile.panel_width == 1448);
  static_assert(kNeo2DeviceProfile.lower_engine_gray_layout == LowerEngineGrayLayout::kLinear);
  static_assert(kNeo2DeviceProfile.panel_height == 1072);
  static_assert(kNeo2DeviceProfile.presentation_to_panel_rotation == 270);
  static_assert(kNeo2DeviceProfile.has_frontlight);
  static_assert(kNeo2DeviceProfile.has_pogo_buttons);
  static_assert(kNeo2DeviceProfile.has_default_nm_resource);
  static_assert(!kNeo2DeviceProfile.has_color_cfa);
  static_assert(kNeo2DeviceProfile.waveform_block_device == "/dev/block/mmcblk0p1");
  static_assert(kNeo2DeviceProfile.token_block_device == "/dev/block/mmcblk0p2");

  static_assert(kOceanDeviceProfile.panel_width == 1264);
  static_assert(kOceanDeviceProfile.lower_engine_gray_layout == LowerEngineGrayLayout::kLinear);
  static_assert(kOceanDeviceProfile.panel_height == 1680);
  static_assert(kOceanDeviceProfile.presentation_to_panel_rotation == 0);
  static_assert(kOceanDeviceProfile.has_frontlight);
  static_assert(kOceanDeviceProfile.has_pogo_buttons);
  static_assert(!kOceanDeviceProfile.has_default_nm_resource);
  static_assert(!kOceanDeviceProfile.has_color_cfa);
  static_assert(kOceanDeviceProfile.waveform_block_device == "/dev/block/by-name/waveform");
  static_assert(kOceanDeviceProfile.token_block_device == "/dev/block/by-name/token");

  static_assert(kMusnapXDeviceProfile.panel_width == 1920);
  static_assert(kMusnapXDeviceProfile.lower_engine_gray_layout ==
                LowerEngineGrayLayout::kAdjacentRowPairsInterleaved);
  static_assert(kMusnapXDeviceProfile.panel_height == 2560);
  static_assert(kMusnapXDeviceProfile.presentation_to_panel_rotation == 0);
  static_assert(!kMusnapXDeviceProfile.has_frontlight);
  static_assert(kMusnapXDeviceProfile.has_pogo_buttons);
  static_assert(!kMusnapXDeviceProfile.has_default_nm_resource);
  static_assert(!kMusnapXDeviceProfile.has_color_cfa);
  static_assert(kMusnapXDeviceProfile.waveform_block_device == "/dev/block/by-name/waveform");
  static_assert(kMusnapXDeviceProfile.token_block_device == "/dev/block/by-name/token");
  static_assert(kMusnapXDeviceProfile.frontlight_calibration_path.empty());

  static_assert(kAuraCDeviceProfile.panel_width == 2480);
  static_assert(kAuraCDeviceProfile.panel_height == 1860);
  static_assert(kAuraCDeviceProfile.presentation_width == 1860);
  static_assert(kAuraCDeviceProfile.presentation_height == 2480);
  static_assert(kAuraCDeviceProfile.presentation_to_panel_rotation == 270);
  static_assert(kAuraCDeviceProfile.lower_engine_gray_layout == LowerEngineGrayLayout::kLinear);
  static_assert(kAuraCDeviceProfile.has_frontlight);
  static_assert(!kAuraCDeviceProfile.has_pogo_buttons);
  static_assert(!kAuraCDeviceProfile.has_default_nm_resource);
  static_assert(kAuraCDeviceProfile.has_color_cfa);
  static_assert(kAuraCDeviceProfile.waveform_block_device == "/dev/block/by-name/waveform");
  static_assert(kAuraCDeviceProfile.token_block_device == "/dev/block/by-name/token");

  assert(FindEinkDeviceProfile("neo2") == &kNeo2DeviceProfile);
  assert(FindEinkDeviceProfile("ocean") == &kOceanDeviceProfile);
  assert(FindEinkDeviceProfile("musnap_x") == &kMusnapXDeviceProfile);
  assert(FindEinkDeviceProfile("aura_c") == &kAuraCDeviceProfile);
  assert(FindEinkDeviceProfile("unknown") == nullptr);
  assert(FindEinkDeviceProfile(1448, 1072) == &kNeo2DeviceProfile);
  assert(FindEinkDeviceProfile(1264, 1680) == &kOceanDeviceProfile);
  assert(FindEinkDeviceProfile(1920, 2560) == &kMusnapXDeviceProfile);
  assert(FindEinkDeviceProfile(2480, 1860) == &kAuraCDeviceProfile);
  assert(FindEinkDeviceProfile(1680, 1264) == nullptr);
#if defined(NEO2_EINK_DEVICE_VARIANT_AURA_C)
  assert(&BuildEinkDeviceProfile() == &kAuraCDeviceProfile);
#elif defined(NEO2_EINK_DEVICE_VARIANT_MUSNAP_X)
  assert(&BuildEinkDeviceProfile() == &kMusnapXDeviceProfile);
#elif defined(NEO2_EINK_DEVICE_VARIANT_OCEAN)
  assert(&BuildEinkDeviceProfile() == &kOceanDeviceProfile);
#else
  assert(&BuildEinkDeviceProfile() == &kNeo2DeviceProfile);
#endif
}
