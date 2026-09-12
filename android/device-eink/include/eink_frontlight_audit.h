#pragma once

#include "eink_frontlight_policy.h"

#include <cstdint>
#include <optional>

namespace neo2::eink {

// A hardware-free audit boundary for B001's direct-light diagnostics. The
// Android backend owns polling and writes; this records only redacted request
// identity and outcomes so the lifecycle has portable test coverage.
struct FrontLightAuditRequest {
  int cold_code = 0;
  int warm_code = 0;
  int warm_percentage = 100;
};

class FrontLightAudit final {
 public:
  struct Diagnostics {
    std::uint64_t request_generations = 0;
    std::uint64_t valid_resolutions = 0;
    std::uint64_t capped_resolutions = 0;
    std::uint64_t controller_applies = 0;
    std::uint64_t zero_off_applies = 0;
    std::uint64_t write_failures = 0;
  };

  // Returns true for a distinct, valid request generation. Raw values remain
  // private to the caller; Diagnostics expose counts only.
  [[nodiscard]] bool ObserveResolution(const FrontLightAuditRequest& request,
                                       const FrontLightResolution& resolution);
  void RecordControllerApply(const FrontLightResolution& resolution);
  void RecordWriteFailure();
  [[nodiscard]] Diagnostics diagnostics() const;

 private:
  std::optional<FrontLightAuditRequest> last_request_;
  Diagnostics diagnostics_;
};

}  // namespace neo2::eink
