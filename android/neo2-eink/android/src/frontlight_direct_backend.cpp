#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "Neo2FrontLight"

#include "neo2/eink/frontlight_direct_backend.h"

#include <eink_frontlight_audit.h>
#include <eink_frontlight_policy.h>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <fcntl.h>
#include <log/log.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unistd.h>

namespace neo2::eink::android {

namespace {

constexpr char kCalibrationPath[] = "/system/etc/neo2_frontlight_calibration.conf";
struct DebugRequest {
  int cold_code = 0;
  int warm_code = 0;
  int warm_percentage = 100;
  bool write_armed = false;
};

struct Payload {
  FrontLightCalibration calibration;
  std::string calibration_id;
  std::string cold_primary_path;
  std::string cold_secondary_path;
  std::string warm_primary_path;
  std::string warm_secondary_path;
  double max_power_multiple = 0.0;
  int warm_percentage = 0;
};

std::optional<int> ParseInt(std::string_view value) {
  int parsed = 0;
  const auto [cursor, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (error != std::errc{} || cursor != value.data() + value.size()) return std::nullopt;
  return parsed;
}

std::optional<double> ParseDouble(std::string_view value) {
  std::string owned(value);
  char* cursor = nullptr;
  errno = 0;
  const double parsed = std::strtod(owned.c_str(), &cursor);
  if (errno != 0 || cursor != owned.c_str() + owned.size() || !std::isfinite(parsed)) {
    return std::nullopt;
  }
  return parsed;
}

template <typename Value, std::size_t Size, typename Parser>
bool ParseArray(std::string_view value, std::array<Value, Size>* output, Parser parser) {
  const auto pieces = ::android::base::Split(std::string(value), ",");
  if (pieces.size() != output->size()) return false;
  for (std::size_t index = 0; index < output->size(); ++index) {
    const auto parsed = parser(::android::base::Trim(pieces[index]));
    if (!parsed) return false;
    if constexpr (std::is_integral_v<Value>) {
      if (*parsed < 0 || *parsed > std::numeric_limits<Value>::max()) return false;
    }
    (*output)[index] = static_cast<Value>(*parsed);
  }
  return true;
}

bool ParsePayload(const std::string& contents, Payload* payload) {
  bool schema_seen = false;
  bool brightness_seen = false;
  bool cold_seen = false;
  bool warm_seen = false;
  bool drive_codes_seen = false;
  bool drive_power_seen = false;
  for (const auto& raw_line : ::android::base::Split(contents, "\n")) {
    const std::string line = ::android::base::Trim(raw_line);
    if (line.empty() || line.starts_with('#')) continue;
    const auto equals = line.find('=');
    if (equals == std::string::npos) return false;
    const auto key = std::string_view(line).substr(0, equals);
    const auto value = std::string_view(line).substr(equals + 1);
    if (key == "schema") {
      schema_seen = value == "neo2-frontlight-v1";
    } else if (key == "calibration_id") {
      payload->calibration_id = value;
    } else if (key == "max_power_multiple") {
      const auto parsed = ParseDouble(value);
      if (!parsed) return false;
      payload->max_power_multiple = *parsed;
    } else if (key == "warm_percentage") {
      const auto parsed = ParseInt(value);
      if (!parsed) return false;
      payload->warm_percentage = *parsed;
    } else if (key == "cold_primary_path") {
      payload->cold_primary_path = value;
    } else if (key == "cold_secondary_path") {
      payload->cold_secondary_path = value;
    } else if (key == "warm_primary_path") {
      payload->warm_primary_path = value;
    } else if (key == "warm_secondary_path") {
      payload->warm_secondary_path = value;
    } else if (key == "brightness_codes") {
      brightness_seen = ParseArray(value, &payload->calibration.brightness_codes, ParseInt);
    } else if (key == "cold_power") {
      cold_seen = ParseArray(value, &payload->calibration.cold_power, ParseDouble);
    } else if (key == "warm_power") {
      warm_seen = ParseArray(value, &payload->calibration.warm_power, ParseDouble);
    } else if (key == "drive_codes") {
      drive_codes_seen = ParseArray(value, &payload->calibration.drive_codes, ParseInt);
    } else if (key == "drive_power") {
      drive_power_seen = ParseArray(value, &payload->calibration.drive_power, ParseDouble);
    } else {
      return false;
    }
  }
  return schema_seen && !payload->calibration_id.empty() && brightness_seen && cold_seen && warm_seen &&
          drive_codes_seen && drive_power_seen && !payload->cold_primary_path.empty() &&
          !payload->warm_primary_path.empty() && std::isfinite(payload->max_power_multiple) &&
          payload->max_power_multiple > 0.0 && payload->warm_percentage > 0;
}

bool CanWrite(const std::string& path) {
  return !path.empty() && access(path.c_str(), W_OK) == 0;
}

bool IsMissingEndpoint(const std::string& path) {
  if (path.empty()) return false;
  if (access(path.c_str(), W_OK) == 0) return false;
  // A calibration may name a second channel that is not populated on this
  // exact board. Distinguish that deterministic absence from an SELinux or
  // permission failure: only the former can safely be treated as optional.
  return errno == ENOENT;
}

bool WriteCode(const std::string& path, std::uint16_t code) {
  if (path.empty()) return true;
  const int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
  if (fd < 0) return false;
  const std::string value = std::to_string(code);
  const ssize_t written = write(fd, value.data(), value.size());
  close(fd);
  return written == static_cast<ssize_t>(value.size());
}

void CopyAuditDiagnostics(const FrontLightAudit::Diagnostics& audit,
                          FrontLightDirectBackend::Diagnostics* diagnostics) {
  diagnostics->request_generations = audit.request_generations;
  diagnostics->valid_resolutions = audit.valid_resolutions;
  diagnostics->capped_resolutions = audit.capped_resolutions;
  diagnostics->controller_applies = audit.controller_applies;
  diagnostics->zero_off_applies = audit.zero_off_applies;
  diagnostics->write_failures = audit.write_failures;
}

}  // namespace

class FrontLightDirectBackend::Impl {
 public:
  mutable std::mutex mutex;
  Diagnostics diagnostics;
  std::optional<Payload> payload;
  std::optional<FrontLightResolution> last_logged;
  std::optional<FrontLightResolution> last_applied;
  FrontLightAudit audit;
  std::chrono::steady_clock::time_point last_request_observed_at{};
  bool preflight_complete = false;
  bool preflight_succeeded = false;
  bool writes_blocked = false;
  bool writes_blocked_logged = false;
  std::optional<bool> last_write_armed;
};

FrontLightDirectBackend::FrontLightDirectBackend() : impl_(std::make_unique<Impl>()) {}

FrontLightDirectBackend::~FrontLightDirectBackend() = default;

bool FrontLightDirectBackend::Poll() {
  std::lock_guard lock(impl_->mutex);
  if (impl_->preflight_complete && !impl_->preflight_succeeded) return false;
  if (!impl_->payload) {
    std::string contents;
    if (!::android::base::ReadFileToString(kCalibrationPath, &contents)) {
      impl_->diagnostics.preflight_failures = kPayloadUnavailable;
      impl_->diagnostics.state = State::kUnavailable;
      impl_->preflight_complete = true;
      ALOGW("front-light payload unavailable; writes remain disarmed");
      return false;
    }
    Payload payload;
    if (!ParsePayload(contents, &payload)) {
      impl_->diagnostics.preflight_failures = kPayloadMalformed;
      impl_->diagnostics.state = State::kRejected;
      impl_->preflight_complete = true;
      ALOGE("front-light payload malformed; writes remain disarmed");
      return false;
    }
    if (!IsValidFrontLightCalibration(payload.calibration)) {
      impl_->diagnostics.preflight_failures = kCalibrationInvalid;
      impl_->diagnostics.state = State::kRejected;
      impl_->preflight_complete = true;
      ALOGE("front-light calibration rejected");
      return false;
    }
    impl_->payload = std::move(payload);
  }

  if (!impl_->preflight_complete) {
    auto& payload = *impl_->payload;
    std::uint32_t failures = kNoPreflightFailure;
    if (!CanWrite(payload.cold_primary_path)) failures |= kPrimaryColdUnavailable;
    if (!CanWrite(payload.warm_primary_path)) failures |= kPrimaryWarmUnavailable;
    if (!payload.cold_secondary_path.empty() && !CanWrite(payload.cold_secondary_path)) {
      if (IsMissingEndpoint(payload.cold_secondary_path)) {
        payload.cold_secondary_path.clear();
        ++impl_->diagnostics.optional_secondary_absent;
        ALOGI("front-light cool secondary endpoint is absent; using the calibrated primary only");
      } else {
        failures |= kSecondaryColdUnavailable;
      }
    }
    if (!payload.warm_secondary_path.empty() && !CanWrite(payload.warm_secondary_path)) {
      if (IsMissingEndpoint(payload.warm_secondary_path)) {
        payload.warm_secondary_path.clear();
        ++impl_->diagnostics.optional_secondary_absent;
        ALOGI("front-light warm secondary endpoint is absent; using the calibrated primary only");
      } else {
        failures |= kSecondaryWarmUnavailable;
      }
    }
    impl_->diagnostics.preflight_failures = failures;
    impl_->preflight_complete = true;
    impl_->preflight_succeeded = failures == kNoPreflightFailure;
    if ((failures & (kPrimaryColdUnavailable | kPrimaryWarmUnavailable)) != 0) {
      impl_->diagnostics.state = State::kUnavailable;
      ALOGE("front-light primary endpoint preflight rejected: failures=0x%x", failures);
      return false;
    }
    if (failures != kNoPreflightFailure) {
      // Do not silently accept a one-sided secondary channel. An armed test
      // can never degrade into an asymmetrical light configuration.
      impl_->diagnostics.state = State::kRejected;
      ALOGE("front-light secondary endpoint preflight rejected: failures=0x%x", failures);
      return false;
    }
    impl_->diagnostics.state = State::kReady;
    ALOGI("front-light preflight ready: primary_channels=2 optional_secondary_absent=%u",
          impl_->diagnostics.optional_secondary_absent);
  }

  if (!impl_->preflight_succeeded) return false;

  const auto now = std::chrono::steady_clock::now();
  ++impl_->diagnostics.polls;

  const auto& payload = *impl_->payload;
  // Retained capture-side observation is permanently disarmed. The
  // process-lifetime Binder service below is the sole direct write route.
  const DebugRequest request{.cold_code = 0, .warm_code = 0,
                             .warm_percentage = payload.warm_percentage, .write_armed = false};
  impl_->diagnostics.write_armed = request.write_armed;
  impl_->diagnostics.writes_blocked = impl_->writes_blocked;
  if (!impl_->last_write_armed || *impl_->last_write_armed != request.write_armed) {
    if (impl_->last_write_armed) ++impl_->diagnostics.arm_transitions;
    ALOGI("front-light diagnostic path is disarmed");
    impl_->last_write_armed = request.write_armed;
  }
  const auto resolution = ResolveFrontLight(
          payload.calibration, {.cold_code = request.cold_code,
                                .warm_code = request.warm_code,
                                .max_power_multiple = payload.max_power_multiple,
                                .warm_percentage = request.warm_percentage});
  if (!resolution) {
    impl_->diagnostics.state = State::kRejected;
    ALOGE("front-light request rejected after validated control parsing");
    return false;
  }
  const bool distinct_request = impl_->audit.ObserveResolution(
          {.cold_code = request.cold_code,
           .warm_code = request.warm_code,
           .warm_percentage = request.warm_percentage},
          *resolution);
  if (distinct_request) {
    impl_->last_request_observed_at = now;
  }
  CopyAuditDiagnostics(impl_->audit.diagnostics(), &impl_->diagnostics);
  impl_->diagnostics.requested_cold_code = request.cold_code;
  impl_->diagnostics.requested_warm_code = request.warm_code;
  impl_->diagnostics.resolved_cold_code = resolution->cold_drive_code;
  impl_->diagnostics.resolved_warm_code = resolution->warm_drive_code;
  if (!impl_->writes_blocked) {
    impl_->diagnostics.state = State::kReady;
  }

  if (!request.write_armed) {
    if (distinct_request) ++impl_->diagnostics.observed_requests;
    if (distinct_request &&
        (!impl_->last_logged || impl_->last_logged->cold_drive_code != resolution->cold_drive_code ||
         impl_->last_logged->warm_drive_code != resolution->warm_drive_code)) {
      ALOGI("front-light observed (disarmed): generation=%llu capped=%d",
            static_cast<unsigned long long>(impl_->diagnostics.request_generations), resolution->capped);
      impl_->last_logged = resolution;
    }
    return true;
  }

  if (impl_->writes_blocked) {
    if (!impl_->writes_blocked_logged) {
      ALOGE("front-light write remains blocked after an earlier failure; reboot the temporary guest");
      impl_->writes_blocked_logged = true;
    }
    return false;
  }

  if (impl_->last_applied && impl_->last_applied->cold_drive_code == resolution->cold_drive_code &&
      impl_->last_applied->warm_drive_code == resolution->warm_drive_code) {
    return true;
  }
  // Keep the observed stock ordering: warm is updated before cool. A failure
  // remains visible in diagnostics and blocks further requests until a new
  // process; it never silently falls back to an uncalibrated endpoint.
  if (!WriteCode(payload.warm_primary_path, resolution->warm_drive_code) ||
      !WriteCode(payload.warm_secondary_path, resolution->warm_drive_code) ||
      !WriteCode(payload.cold_primary_path, resolution->cold_drive_code) ||
      !WriteCode(payload.cold_secondary_path, resolution->cold_drive_code)) {
    impl_->audit.RecordWriteFailure();
    CopyAuditDiagnostics(impl_->audit.diagnostics(), &impl_->diagnostics);
    impl_->diagnostics.state = State::kWriteFailed;
    impl_->writes_blocked = true;
    impl_->diagnostics.writes_blocked = true;
    ALOGE("front-light controller apply failed");
    return false;
  }
  ++impl_->diagnostics.applied_requests;
  impl_->audit.RecordControllerApply(*resolution);
  CopyAuditDiagnostics(impl_->audit.diagnostics(), &impl_->diagnostics);
  if (impl_->last_request_observed_at != std::chrono::steady_clock::time_point{}) {
    impl_->diagnostics.latest_observed_to_apply_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - impl_->last_request_observed_at)
                    .count());
  }
  impl_->last_applied = resolution;
  impl_->diagnostics.state = State::kReady;
  ALOGI("front-light applied: generation=%llu zero_off=%d",
        static_cast<unsigned long long>(impl_->diagnostics.request_generations),
        resolution->cold_drive_code == 0 && resolution->warm_drive_code == 0);
  return true;
}

FrontLightDirectBackend::Diagnostics FrontLightDirectBackend::diagnostics() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->diagnostics;
}

neo2::eink::FrontLightControlOutcome FrontLightDirectBackend::ApplyManualRequest(
        const neo2::eink::ManualFrontLightRequest& request) {
  // Poll does the fail-closed payload and endpoint preflight. Its retained
  // diagnostic request is always disarmed, so this explicit privileged call is
  // the only app-facing write route.
  if (!Poll()) return neo2::eink::FrontLightControlOutcome::kUnavailable;
  std::lock_guard lock(impl_->mutex);
  if (!impl_->preflight_succeeded || !impl_->payload || impl_->writes_blocked) {
    return neo2::eink::FrontLightControlOutcome::kUnavailable;
  }
  const auto& payload = *impl_->payload;
  const auto resolution = ResolveFrontLightPercent(payload.calibration, request.brightness_percent,
                                                   request.warmth_percent, payload.max_power_multiple,
                                                   payload.warm_percentage);
  if (!resolution) return neo2::eink::FrontLightControlOutcome::kRejected;
  if (!WriteCode(payload.warm_primary_path, resolution->warm_drive_code) ||
      !WriteCode(payload.warm_secondary_path, resolution->warm_drive_code) ||
      !WriteCode(payload.cold_primary_path, resolution->cold_drive_code) ||
      !WriteCode(payload.cold_secondary_path, resolution->cold_drive_code)) {
    impl_->audit.RecordWriteFailure();
    CopyAuditDiagnostics(impl_->audit.diagnostics(), &impl_->diagnostics);
    impl_->writes_blocked = true;
    impl_->diagnostics.writes_blocked = true;
    impl_->diagnostics.state = State::kWriteFailed;
    return neo2::eink::FrontLightControlOutcome::kFailed;
  }
  impl_->audit.RecordControllerApply(*resolution);
  CopyAuditDiagnostics(impl_->audit.diagnostics(), &impl_->diagnostics);
  impl_->last_applied = resolution;
  impl_->diagnostics.state = State::kReady;
  if (request.brightness_percent == 0) return neo2::eink::FrontLightControlOutcome::kOff;
  return resolution->capped ? neo2::eink::FrontLightControlOutcome::kCapped
                            : neo2::eink::FrontLightControlOutcome::kApplied;
}

}  // namespace neo2::eink::android
