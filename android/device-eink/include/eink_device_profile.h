#pragma once

#include <cstdint>
#include <string_view>

namespace neo2::eink {

enum class EinkDeviceVariant : std::uint8_t {
  kNeo2,
  kOcean,
  kMusnapX,
  kAuraC,
};

enum class LowerEngineGrayLayout : std::uint8_t {
  kLinear,
  kAdjacentRowPairsInterleaved,
};

// Product behavior remains common. This record contains only the immutable
// panel and hardware-capability boundary selected by the build.
struct EinkDeviceProfile {
  EinkDeviceVariant variant;
  std::string_view name;
  std::uint32_t panel_width;
  std::uint32_t panel_height;
  std::uint32_t presentation_width;
  std::uint32_t presentation_height;
  int presentation_to_panel_rotation;
  LowerEngineGrayLayout lower_engine_gray_layout;
  bool has_frontlight;
  bool has_pogo_buttons;
  bool has_default_nm_resource;
  bool has_color_cfa;
  std::string_view waveform_block_device;
  std::string_view token_block_device;
  std::string_view frontlight_calibration_path;
  // New models remain capture-only and finite until explicitly qualified.
  bool display_enabled_by_default = false;
};

inline constexpr EinkDeviceProfile kNeo2DeviceProfile{
        .variant = EinkDeviceVariant::kNeo2,
        .name = "neo2",
        .panel_width = 1448,
        .panel_height = 1072,
        .presentation_width = 1072,
        .presentation_height = 1448,
        .presentation_to_panel_rotation = 270,
        .lower_engine_gray_layout = LowerEngineGrayLayout::kLinear,
        .has_frontlight = true,
        .has_pogo_buttons = true,
        .has_default_nm_resource = true,
        .has_color_cfa = false,
        .waveform_block_device = "/dev/block/mmcblk0p1",
        .token_block_device = "/dev/block/mmcblk0p2",
        .frontlight_calibration_path = "/system/etc/neo2_frontlight_calibration.conf",
        .display_enabled_by_default = true,
};

inline constexpr EinkDeviceProfile kOceanDeviceProfile{
        .variant = EinkDeviceVariant::kOcean,
        .name = "ocean",
        .panel_width = 1264,
        .panel_height = 1680,
        .presentation_width = 1264,
        .presentation_height = 1680,
        .presentation_to_panel_rotation = 0,
        .lower_engine_gray_layout = LowerEngineGrayLayout::kLinear,
        .has_frontlight = true,
        .has_pogo_buttons = true,
        .has_default_nm_resource = false,
        .has_color_cfa = false,
        .waveform_block_device = "/dev/block/by-name/waveform",
        .token_block_device = "/dev/block/by-name/token",
        .frontlight_calibration_path = "/system/etc/ocean_frontlight_calibration.conf",
};

inline constexpr EinkDeviceProfile kMusnapXDeviceProfile{
        .variant = EinkDeviceVariant::kMusnapX,
        .name = "musnap_x",
        .panel_width = 1920,
        .panel_height = 2560,
        .presentation_width = 1920,
        .presentation_height = 2560,
        .presentation_to_panel_rotation = 0,
        .lower_engine_gray_layout = LowerEngineGrayLayout::kAdjacentRowPairsInterleaved,
        .has_frontlight = false,
        .has_pogo_buttons = true,
        .has_default_nm_resource = false,
        .has_color_cfa = false,
        .waveform_block_device = "/dev/block/by-name/waveform",
        .token_block_device = "/dev/block/by-name/token",
        .frontlight_calibration_path = "",
};

// The framework composes a portrait 1860x2480 surface. The stock output path
// rotates that into a 2480x1860 panel-facing frame before its separate
// Kaleido CFA stage produces the controller's 3840x1280 electrical layout.
// The electrical geometry is intentionally not represented as panel geometry:
// it belongs to the Aura-only color backend and must never size generic gray
// capture buffers.
inline constexpr EinkDeviceProfile kAuraCDeviceProfile{
        .variant = EinkDeviceVariant::kAuraC,
        .name = "aura_c",
        .panel_width = 2480,
        .panel_height = 1860,
        .presentation_width = 1860,
        .presentation_height = 2480,
        .presentation_to_panel_rotation = 270,
        .lower_engine_gray_layout = LowerEngineGrayLayout::kLinear,
        .has_frontlight = true,
        .has_pogo_buttons = false,
        .has_default_nm_resource = false,
        .has_color_cfa = true,
        .waveform_block_device = "/dev/block/by-name/waveform",
        .token_block_device = "/dev/block/by-name/token",
        .frontlight_calibration_path = "/system/etc/aura_c_frontlight_calibration.conf",
        .display_enabled_by_default = true,
};

[[nodiscard]] constexpr const EinkDeviceProfile* FindEinkDeviceProfile(
        std::string_view name) {
  if (name == kNeo2DeviceProfile.name) return &kNeo2DeviceProfile;
  if (name == kOceanDeviceProfile.name) return &kOceanDeviceProfile;
  if (name == kMusnapXDeviceProfile.name) return &kMusnapXDeviceProfile;
  if (name == kAuraCDeviceProfile.name) return &kAuraCDeviceProfile;
  return nullptr;
}

[[nodiscard]] constexpr const EinkDeviceProfile* FindEinkDeviceProfile(
        std::uint32_t panel_width, std::uint32_t panel_height) {
  if (panel_width == kNeo2DeviceProfile.panel_width &&
      panel_height == kNeo2DeviceProfile.panel_height) {
    return &kNeo2DeviceProfile;
  }
  if (panel_width == kOceanDeviceProfile.panel_width &&
      panel_height == kOceanDeviceProfile.panel_height) {
    return &kOceanDeviceProfile;
  }
  if (panel_width == kMusnapXDeviceProfile.panel_width &&
      panel_height == kMusnapXDeviceProfile.panel_height) {
    return &kMusnapXDeviceProfile;
  }
  if (panel_width == kAuraCDeviceProfile.panel_width &&
      panel_height == kAuraCDeviceProfile.panel_height) {
    return &kAuraCDeviceProfile;
  }
  return nullptr;
}

// Historic B038 and host-test builds intentionally retain Neo as the default.
// Android product modules must define exactly one variant macro.
[[nodiscard]] constexpr const EinkDeviceProfile& BuildEinkDeviceProfile() {
#if (defined(NEO2_EINK_DEVICE_VARIANT_NEO2) + defined(NEO2_EINK_DEVICE_VARIANT_OCEAN) + \
     defined(NEO2_EINK_DEVICE_VARIANT_MUSNAP_X) + \
     defined(NEO2_EINK_DEVICE_VARIANT_AURA_C)) > 1
#error "multiple e-ink device variants selected"
#elif defined(NEO2_EINK_DEVICE_VARIANT_AURA_C)
  return kAuraCDeviceProfile;
#elif defined(NEO2_EINK_DEVICE_VARIANT_MUSNAP_X)
  return kMusnapXDeviceProfile;
#elif defined(NEO2_EINK_DEVICE_VARIANT_OCEAN)
  return kOceanDeviceProfile;
#else
  return kNeo2DeviceProfile;
#endif
}

}  // namespace neo2::eink
