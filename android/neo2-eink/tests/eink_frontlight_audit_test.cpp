#include "eink_frontlight_audit.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  neo2::eink::FrontLightAudit audit;
  const neo2::eink::FrontLightAuditRequest request{.cold_code = 4,
                                                    .warm_code = 8,
                                                    .warm_percentage = 100};
  const neo2::eink::FrontLightResolution capped{.cold_drive_code = 9,
                                                 .warm_drive_code = 7,
                                                 .capped = true};
  Require(audit.ObserveResolution(request, capped), "first valid request was not generated");
  Require(!audit.ObserveResolution(request, capped), "equal request created a new generation");
  audit.RecordWriteFailure();
  const neo2::eink::FrontLightResolution off{.cold_drive_code = 0, .warm_drive_code = 0};
  audit.RecordControllerApply(off);
  const auto diagnostics = audit.diagnostics();
  Require(diagnostics.request_generations == 1 && diagnostics.valid_resolutions == 1 &&
                  diagnostics.capped_resolutions == 1 && diagnostics.write_failures == 1 &&
                  diagnostics.controller_applies == 1 && diagnostics.zero_off_applies == 1,
          "front-light audit counters did not retain the redacted lifecycle");
  std::cout << "PASS\n";
  return 0;
}
