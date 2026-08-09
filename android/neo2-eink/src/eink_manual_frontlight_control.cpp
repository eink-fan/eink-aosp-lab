#include "eink_manual_frontlight_control.h"

namespace neo2::eink {

ManualFrontLightControl::ManualFrontLightControl(FrontLightControlBackend& backend) : backend_(backend) {}

bool IsValidManualFrontLightRequest(const ManualFrontLightRequest& request) {
  return request.brightness_percent >= 0 && request.brightness_percent <= 100 &&
          request.warmth_percent >= 0 && request.warmth_percent <= 100;
}

FrontLightControlOutcome ManualFrontLightControl::Apply(const ManualFrontLightRequest& request) {
  if (failure_latched_) return FrontLightControlOutcome::kFailureLatched;
  if (!IsValidManualFrontLightRequest(request)) return FrontLightControlOutcome::kRejected;

  const FrontLightControlOutcome outcome = backend_.ApplyManualRequest(request);
  if (outcome == FrontLightControlOutcome::kFailed) failure_latched_ = true;
  if (request.brightness_percent == 0 && outcome == FrontLightControlOutcome::kApplied) {
    // A zero request can be called off only if the calibrated backend confirms
    // the explicit off outcome. Never invent that physical-state claim here.
    return FrontLightControlOutcome::kFailed;
  }
  return outcome;
}

bool ManualFrontLightControl::failure_latched() const {
  return failure_latched_;
}

}  // namespace neo2::eink
