#pragma once

#include <cstdint>

namespace neo2::eink {

// Redacted result values shared by the platform Binder service and its thin
// dogfood UI. They deliberately reveal neither calibration output nor any
// inferred physical light state.
enum class FrontLightControlOutcome : std::uint8_t {
  kApplied,
  kCapped,
  kOff,
  kUnavailable,
  kRejected,
  kFailed,
  kFailureLatched,
};

struct ManualFrontLightRequest {
  int brightness_percent = 0;
  int warmth_percent = 0;
};

class FrontLightControlBackend {
 public:
  virtual ~FrontLightControlBackend() = default;

  // The implementation owns calibration, endpoint preflight, and I/O. It
  // receives normalized percentages only and returns a redacted outcome.
  [[nodiscard]] virtual FrontLightControlOutcome ApplyManualRequest(
          const ManualFrontLightRequest& request) = 0;
};

// Process-lifetime, non-persistent front-light request boundary. UI changes
// have no representation here; callers invoke Apply only for an explicit
// Apply or Turn off action. A hardware failure latches this service instance
// closed until its owning process is deliberately restarted.
class ManualFrontLightControl final {
 public:
  explicit ManualFrontLightControl(FrontLightControlBackend& backend);

  [[nodiscard]] FrontLightControlOutcome Apply(const ManualFrontLightRequest& request);
  [[nodiscard]] bool failure_latched() const;

 private:
  FrontLightControlBackend& backend_;
  bool failure_latched_ = false;
};

[[nodiscard]] bool IsValidManualFrontLightRequest(const ManualFrontLightRequest& request);

}  // namespace neo2::eink
