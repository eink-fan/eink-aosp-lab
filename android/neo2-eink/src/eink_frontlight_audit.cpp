#include "eink_frontlight_audit.h"

namespace neo2::eink {

bool FrontLightAudit::ObserveResolution(const FrontLightAuditRequest& request,
                                        const FrontLightResolution& resolution) {
  const bool distinct = !last_request_ || last_request_->cold_code != request.cold_code ||
          last_request_->warm_code != request.warm_code ||
          last_request_->warm_percentage != request.warm_percentage;
  if (!distinct) return false;
  last_request_ = request;
  ++diagnostics_.request_generations;
  ++diagnostics_.valid_resolutions;
  if (resolution.capped) ++diagnostics_.capped_resolutions;
  return true;
}

void FrontLightAudit::RecordControllerApply(const FrontLightResolution& resolution) {
  ++diagnostics_.controller_applies;
  if (resolution.cold_drive_code == 0 && resolution.warm_drive_code == 0) {
    ++diagnostics_.zero_off_applies;
  }
}

void FrontLightAudit::RecordWriteFailure() {
  ++diagnostics_.write_failures;
}

FrontLightAudit::Diagnostics FrontLightAudit::diagnostics() const {
  return diagnostics_;
}

}  // namespace neo2::eink
