#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace neo2::eink {

// The device-specific values for this policy are deliberately a runtime
// payload, not source. This structure is the cleared policy contract shared
// by host tests and the Android-only controller boundary.
constexpr std::size_t kFrontLightSteps = 31;
constexpr std::size_t kFrontLightDrivePoints = 252;

struct FrontLightCalibration {
  std::array<std::uint16_t, kFrontLightSteps> brightness_codes{};
  std::array<double, kFrontLightSteps> cold_power{};
  std::array<double, kFrontLightSteps> warm_power{};
  std::array<std::uint16_t, kFrontLightDrivePoints> drive_codes{};
  std::array<double, kFrontLightDrivePoints> drive_power{};
};

struct FrontLightRequest {
  // These are the raw persisted values, not a UI slider position. The policy
  // normalizes each to the nearest calibrated discrete code before resolving
  // controller output.
  int cold_code = 0;
  int warm_code = 0;
  double max_power_multiple = 1.0;
  int warm_percentage = 100;
};

struct FrontLightResolution {
  std::size_t cold_step = 0;
  std::size_t warm_step = 0;
  std::size_t cold_drive_step = 0;
  std::size_t warm_drive_step = 0;
  std::uint16_t normalized_cold_code = 0;
  std::uint16_t normalized_warm_code = 0;
  std::uint16_t cold_drive_code = 0;
  std::uint16_t warm_drive_code = 0;
  double requested_total_power = 0.0;
  double applied_total_power = 0.0;
  double applied_cold_power = 0.0;
  double applied_warm_power = 0.0;
  bool capped = false;
};

// Maps product-facing percentages onto the calibrated brightness steps before
// applying the existing two-channel resolver. It is not a raw-drive API.
[[nodiscard]] std::optional<FrontLightResolution> ResolveFrontLightPercent(
        const FrontLightCalibration& calibration, int brightness_percent, int warmth_percent,
        double max_power_multiple, int warm_percentage);

// Bounds for temporary-guest debug control values. These are raw persisted
// code values; ResolveFrontLight still normalizes them against the verified
// calibration before any controller value is produced.
[[nodiscard]] bool IsValidFrontLightDebugRequest(int cold_code, int warm_code,
                                                  int warm_percentage);

// Checks only the generic shape needed for safe resolution. It intentionally
// does not bless an arbitrary calibration for hardware use; the Android
// boundary additionally verifies the payload identity before it can write.
[[nodiscard]] bool IsValidFrontLightCalibration(const FrontLightCalibration& calibration);

// Reproduces the stock controller's calibrated two-channel allocation without
// performing I/O. Invalid calibration or runtime controls fail closed.
[[nodiscard]] std::optional<FrontLightResolution> ResolveFrontLight(
        const FrontLightCalibration& calibration, const FrontLightRequest& request);

}  // namespace neo2::eink
