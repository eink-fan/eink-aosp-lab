#include "eink_frontlight_policy.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace neo2::eink {

namespace {

template <typename Value, std::size_t Size>
std::size_t NearestIndex(const std::array<Value, Size>& values, const Value target) {
  std::size_t best = 0;
  auto best_distance = std::abs(values.front() - target);
  for (std::size_t index = 1; index < values.size(); ++index) {
    const auto distance = std::abs(values[index] - target);
    // Preserve the earlier entry on a tie, matching the stock scan's strict
    // comparison while avoiding a hidden rounding direction.
    if (distance < best_distance) {
      best = index;
      best_distance = distance;
    }
  }
  return best;
}

bool IsFiniteNonNegative(double value) {
  return std::isfinite(value) && value >= 0.0;
}

}  // namespace

bool IsValidFrontLightDebugRequest(int cold_code, int warm_code, int warm_percentage) {
  return cold_code >= 0 && cold_code <= 65535 && warm_code >= 0 && warm_code <= 65535 &&
          warm_percentage >= 1;
}

bool IsValidFrontLightBrightnessCodes(
        const std::array<std::uint16_t, kFrontLightSteps>& brightness_codes) {
  if (brightness_codes.front() != 0) return false;
  for (std::size_t index = 1; index < brightness_codes.size(); ++index) {
    if (brightness_codes[index] <= brightness_codes[index - 1]) return false;
  }
  return true;
}

bool IsValidFrontLightCalibration(const FrontLightCalibration& calibration) {
  if (!IsValidFrontLightBrightnessCodes(calibration.brightness_codes)) return false;
  for (std::size_t index = 1; index < calibration.drive_codes.size(); ++index) {
    if (calibration.drive_codes[index] <= calibration.drive_codes[index - 1]) {
      return false;
    }
  }
  return std::all_of(calibration.cold_power.begin(), calibration.cold_power.end(),
                     IsFiniteNonNegative) &&
          std::all_of(calibration.warm_power.begin(), calibration.warm_power.end(),
                      IsFiniteNonNegative) &&
          std::all_of(calibration.drive_power.begin(), calibration.drive_power.end(),
                      IsFiniteNonNegative);
}

std::optional<DirectFrontLightResolution> ResolveDirectFrontLightPercent(
        const std::array<std::uint16_t, kFrontLightSteps>& brightness_codes,
        int brightness_percent, int warmth_percent) {
  if (brightness_percent < 0 || brightness_percent > 100 || warmth_percent < 0 ||
      warmth_percent > 100 || !IsValidFrontLightBrightnessCodes(brightness_codes)) {
    return std::nullopt;
  }
  const auto intensity_step = static_cast<std::size_t>(
          (brightness_percent * static_cast<int>(kFrontLightSteps - 1) + 50) / 100);
  const auto warm_step =
          (intensity_step * static_cast<std::size_t>(warmth_percent) + 50) / 100;
  const auto cold_step = intensity_step - warm_step;
  return DirectFrontLightResolution{
          .cold_step = cold_step,
          .warm_step = warm_step,
          .cold_drive_code = brightness_codes[cold_step],
          .warm_drive_code = brightness_codes[warm_step],
  };
}

std::optional<FrontLightResolution> ResolveFrontLight(
        const FrontLightCalibration& calibration, const FrontLightRequest& request) {
  if (!IsValidFrontLightCalibration(calibration) || !std::isfinite(request.max_power_multiple) ||
      request.max_power_multiple <= 0.0 || request.warm_percentage <= 0) {
    return std::nullopt;
  }

  const auto cold_step = NearestIndex(calibration.brightness_codes,
                                      static_cast<std::uint16_t>(std::clamp(request.cold_code, 0, 65535)));
  const auto warm_step = NearestIndex(calibration.brightness_codes,
                                      static_cast<std::uint16_t>(std::clamp(request.warm_code, 0, 65535)));
  const double cold_power = calibration.cold_power[cold_step];
  const double warm_power = calibration.warm_power[warm_step];
  const double requested_total_power = cold_power + warm_power;
  const double maximum_total_power = calibration.cold_power.back() * request.max_power_multiple;
  const double applied_total_power = std::min(requested_total_power, maximum_total_power);

  // The stock policy uses the discrete step indices to preserve its hue
  // behaviour, rather than simply keeping the ratio of the two power curves.
  const auto denominator = cold_step + warm_step;
  double warm_target_power = denominator == 0
          ? 0.0
          : applied_total_power * static_cast<double>(warm_step) /
                  static_cast<double>(denominator);
  if (warm_step > 20) {
    warm_target_power *= static_cast<double>(request.warm_percentage) / 100.0;
  }
  warm_target_power = std::clamp(warm_target_power, 0.0, applied_total_power);

  const auto warm_drive_step = NearestIndex(calibration.drive_power, warm_target_power);
  const double resolved_warm_power = calibration.drive_power[warm_drive_step];
  const double cold_target_power = std::max(0.0, applied_total_power - resolved_warm_power);
  const auto cold_drive_step = NearestIndex(calibration.drive_power, cold_target_power);

  return FrontLightResolution{
          .cold_step = cold_step,
          .warm_step = warm_step,
          .cold_drive_step = cold_drive_step,
          .warm_drive_step = warm_drive_step,
          .normalized_cold_code = calibration.brightness_codes[cold_step],
          .normalized_warm_code = calibration.brightness_codes[warm_step],
          .cold_drive_code = calibration.drive_codes[cold_drive_step],
          .warm_drive_code = calibration.drive_codes[warm_drive_step],
          .requested_total_power = requested_total_power,
          .applied_total_power = applied_total_power,
          .applied_cold_power = calibration.drive_power[cold_drive_step],
          .applied_warm_power = resolved_warm_power,
          .capped = requested_total_power > maximum_total_power,
  };
}

std::optional<FrontLightResolution> ResolveFrontLightPercent(
        const FrontLightCalibration& calibration, int brightness_percent, int warmth_percent,
        double max_power_multiple, int warm_percentage) {
  if (brightness_percent < 0 || brightness_percent > 100 || warmth_percent < 0 ||
      warmth_percent > 100 || !IsValidFrontLightCalibration(calibration)) {
    return std::nullopt;
  }
  const auto intensity_step = static_cast<std::size_t>(
          (brightness_percent * static_cast<int>(kFrontLightSteps - 1) + 50) / 100);
  const auto cold_step = intensity_step * static_cast<std::size_t>(100 - warmth_percent) / 100;
  const auto warm_step = intensity_step * static_cast<std::size_t>(warmth_percent) / 100;
  return ResolveFrontLight(calibration,
                           {.cold_code = calibration.brightness_codes[cold_step],
                            .warm_code = calibration.brightness_codes[warm_step],
                            .max_power_multiple = max_power_multiple,
                            .warm_percentage = warm_percentage});
}

}  // namespace neo2::eink
