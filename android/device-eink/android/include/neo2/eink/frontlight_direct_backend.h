#pragma once

#include <cstdint>
#include <memory>

#include <eink_manual_frontlight_control.h>

namespace neo2::eink::android {

// Same-build front-light boundary. Its calibration file, endpoint names, and
// payload identity are restricted runtime inputs. The retained Poll() path is
// permanently disarmed; only the private Binder service may request a write.
class FrontLightDirectBackend final : public neo2::eink::FrontLightControlBackend {
 public:
  enum class State : std::uint8_t {
    kStopped,
    kReady,
    kUnavailable,
    kRejected,
    kWriteFailed,
  };

  enum PreflightFailure : std::uint32_t {
    kNoPreflightFailure = 0,
    kPayloadUnavailable = 1U << 0,
    kPayloadMalformed = 1U << 1,
    kCalibrationInvalid = 1U << 2,
    kPrimaryColdUnavailable = 1U << 3,
    kPrimaryWarmUnavailable = 1U << 4,
    kSecondaryColdUnavailable = 1U << 5,
    kSecondaryWarmUnavailable = 1U << 6,
  };

  struct Diagnostics {
    State state = State::kStopped;
    std::uint32_t preflight_failures = kNoPreflightFailure;
    std::uint64_t polls = 0;
    std::uint64_t polls_skipped = 0;
    std::uint64_t poll_interval_rejections = 0;
    std::uint64_t request_generations = 0;
    std::uint64_t valid_resolutions = 0;
    std::uint64_t capped_resolutions = 0;
    std::uint64_t controller_applies = 0;
    std::uint64_t zero_off_applies = 0;
    std::uint64_t latest_observed_to_apply_ns = 0;
    std::uint64_t observed_requests = 0;
    std::uint64_t applied_requests = 0;
    std::uint64_t write_failures = 0;
    std::uint64_t debug_control_rejections = 0;
    std::uint64_t arm_transitions = 0;
    std::uint32_t optional_secondary_absent = 0;
    bool write_armed = false;
    bool writes_blocked = false;
    int requested_cold_code = 0;
    int requested_warm_code = 0;
    int resolved_cold_code = 0;
    int resolved_warm_code = 0;
  };

  FrontLightDirectBackend();
  ~FrontLightDirectBackend();

  FrontLightDirectBackend(const FrontLightDirectBackend&) = delete;
  FrontLightDirectBackend& operator=(const FrontLightDirectBackend&) = delete;

  // Retained diagnostic observation only. It is permanently disarmed and
  // validates/logs a calibrated zero request without writing hardware.
  [[nodiscard]] bool Poll();
  [[nodiscard]] neo2::eink::FrontLightControlOutcome ApplyManualRequest(
          const neo2::eink::ManualFrontLightRequest& request) override;
  [[nodiscard]] Diagnostics diagnostics() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace neo2::eink::android
