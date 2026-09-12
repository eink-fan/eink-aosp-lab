#include "eink_frontlight_policy.h"

#include <cassert>

namespace {

neo2::eink::FrontLightCalibration MakeCalibration() {
  neo2::eink::FrontLightCalibration calibration;
  for (std::size_t index = 0; index < neo2::eink::kFrontLightSteps; ++index) {
    calibration.brightness_codes[index] = static_cast<std::uint16_t>(index * 8);
    calibration.cold_power[index] = static_cast<double>(index);
    calibration.warm_power[index] = static_cast<double>(index) / 2.0;
  }
  for (std::size_t index = 0; index < neo2::eink::kFrontLightDrivePoints; ++index) {
    calibration.drive_codes[index] = static_cast<std::uint16_t>(index + 1);
    calibration.drive_power[index] = static_cast<double>(index) / 10.0;
  }
  return calibration;
}

void ResolvesAndNormalizesBothChannels() {
  const auto calibration = MakeCalibration();
  const auto resolution = neo2::eink::ResolveFrontLight(
          calibration, {.cold_code = 65, .warm_code = 25, .max_power_multiple = 2.0,
                        .warm_percentage = 100});
  assert(resolution.has_value());
  assert(resolution->cold_step == 8);
  assert(resolution->warm_step == 3);
  assert(resolution->normalized_cold_code == 64);
  assert(resolution->normalized_warm_code == 24);
  assert(!resolution->capped);
  // Total power is 9.5. The stock allocation uses 3/(8+3), yielding a
  // 2.59 warm target, then nearest calibrated outputs for each channel.
  assert(resolution->warm_drive_step == 26);
  assert(resolution->cold_drive_step == 69);
}

void CapsTotalPowerBeforeAllocation() {
  const auto calibration = MakeCalibration();
  const auto resolution = neo2::eink::ResolveFrontLight(
          calibration, {.cold_code = 240, .warm_code = 240, .max_power_multiple = 1.0,
                        .warm_percentage = 120});
  assert(resolution.has_value());
  assert(resolution->capped);
  assert(resolution->applied_total_power == 30.0);
  assert(resolution->applied_warm_power <= resolution->applied_total_power);
  assert(resolution->applied_cold_power <= resolution->applied_total_power);
}

void RejectsInvalidInputs() {
  auto calibration = MakeCalibration();
  calibration.drive_codes[40] = calibration.drive_codes[39];
  assert(!neo2::eink::IsValidFrontLightCalibration(calibration));
  assert(!neo2::eink::ResolveFrontLight(calibration, {}).has_value());

  calibration = MakeCalibration();
  assert(!neo2::eink::ResolveFrontLight(
                  calibration, {.max_power_multiple = 0.0, .warm_percentage = 100})
                  .has_value());
}

void ResolvesProductPercentagesThroughCalibration() {
  const auto calibration = MakeCalibration();
  const auto off = neo2::eink::ResolveFrontLightPercent(calibration, 0, 50, 1.0, 100);
  assert(off.has_value());
  assert(off->cold_drive_code == calibration.drive_codes.front());
  assert(off->warm_drive_code == calibration.drive_codes.front());
  assert(!neo2::eink::ResolveFrontLightPercent(calibration, 101, 50, 1.0, 100).has_value());
  assert(!neo2::eink::ResolveFrontLightPercent(calibration, 50, -1, 1.0, 100).has_value());
}

void ResolvesDirectObservedCodesWithoutInventingPowerData() {
  std::array<std::uint16_t, neo2::eink::kFrontLightSteps> codes{};
  for (std::size_t index = 1; index < codes.size(); ++index) {
    codes[index] = static_cast<std::uint16_t>(index + 40);
  }
  const auto balanced = neo2::eink::ResolveDirectFrontLightPercent(codes, 100, 50);
  assert(balanced.has_value());
  assert(balanced->cold_step == 15);
  assert(balanced->warm_step == 15);
  assert(balanced->cold_drive_code == codes[15]);
  assert(balanced->warm_drive_code == codes[15]);

  const auto cold = neo2::eink::ResolveDirectFrontLightPercent(codes, 100, 0);
  assert(cold.has_value());
  assert(cold->cold_step == 30);
  assert(cold->warm_step == 0);
  assert(cold->cold_drive_code == codes.back());
  assert(cold->warm_drive_code == 0);

  codes[10] = codes[9];
  assert(!neo2::eink::ResolveDirectFrontLightPercent(codes, 50, 50).has_value());
}

}  // namespace

int main() {
  assert(neo2::eink::IsValidFrontLightDebugRequest(0, 65535, 120));
  assert(!neo2::eink::IsValidFrontLightDebugRequest(-1, 0, 100));
  assert(!neo2::eink::IsValidFrontLightDebugRequest(0, 65536, 100));
  assert(!neo2::eink::IsValidFrontLightDebugRequest(0, 0, 0));
  assert(!neo2::eink::IsValidFrontLightDebugRequest(0, 0, -1));
  ResolvesAndNormalizesBothChannels();
  CapsTotalPowerBeforeAllocation();
  RejectsInvalidInputs();
  ResolvesProductPercentagesThroughCalibration();
  ResolvesDirectObservedCodesWithoutInventingPowerData();
}
