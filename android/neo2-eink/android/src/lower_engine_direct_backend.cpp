#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "Neo2EinkLowerEngine"

#include "neo2/eink/lower_engine_direct_backend.h"

#include <android-base/properties.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <log/log.h>
#include <ui/GraphicBuffer.h>
#include <utils/StrongPointer.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <unistd.h>

namespace neo2::eink::android {

namespace {

constexpr int kPanelWidth = 1448;
constexpr int kPanelHeight = 1072;
constexpr int kObservedFullControlRecord =
        ToEngineMode(RefreshMode::kGc16Partial) + ToEngineMode(RefreshMode::kFullUpdate);
constexpr int kObservedNormalDifferentialRecord = ToEngineMode(RefreshMode::kGlr16Partial);
// The observed normal `(2, 1, 1)` route calls pre_process_update_image(),
// and the observed normal `(3, 0, 0)` route reaches that same return. It is
// not a POSIX-style error.
constexpr int kObservedUpdateAcceptedResult = -3;
// Finite operation spans normal boot into the launcher. Continuous operation
// is separately and explicitly armed by a volatile guest debug property; its
// cadence floor prevents a high-rate panel loop in either mode.
constexpr std::uint64_t kM4DefaultSubmissionLimit = 12;
constexpr std::uint64_t kM4HardSubmissionLimit = 24;
constexpr int kM4DefaultMinimumIntervalMs = 50;
// Android 17's reader default is the already tested 50-ms safety floor. The
// engine still owns waveform work; this is an output-rate floor, never a
// panel-completion claim.
constexpr int kM4MinimumIntervalMs = 50;
constexpr int kM4MaximumIntervalMs = 5000;
constexpr int kM4MinimumCompletionObservationMs = 100;
constexpr int kM4DefaultCompletionObservationMs = 1000;
constexpr int kM4MaximumCompletionObservationMs = 10000;
constexpr char kM4SubmissionLimitProperty[] = "debug.neo2.eink.max_updates";
constexpr char kM4MinimumIntervalProperty[] = "debug.neo2.eink.min_interval_ms";
constexpr char kM4ContinuousProperty[] = "debug.neo2.eink.continuous";
constexpr char kM4EnabledProperty[] = "debug.neo2.eink.enabled";
constexpr char kM4CompletionObservationProperty[] = "debug.neo2.eink.completion_observation_ms";

constexpr char kEngineLibrary[] = "libidisplayengine.so";
constexpr char kDefaultWaveform[] = "/system/etc/default_wbf.bin";
constexpr char kRegalWaveform[] = "/system/etc/regal.wbf";
constexpr char kDefaultNm[] = "/system/etc/default_nm.bin";
constexpr char kSwtconDevice[] = "/dev/swtcon_power_cdev";
constexpr char kZhangyueDevice[] = "/dev/zhangyuecdev";
constexpr char kPixFormat[] = "/sys/eink/burn/pix_fmt";
constexpr char kVcomValue[] = "/sys/eink/app/vcomvalue";
constexpr char kTemperatureInfo[] = "/dev/einktemperinfo";
constexpr char kWaveformBlockDevice[] = "/dev/block/mmcblk0p1";
constexpr char kTokenBlockDevice[] = "/dev/block/mmcblk0p2";
constexpr char kBridgeProperty[] = "ro.eink.fpga.bridge.support";

constexpr int NormalizeMinimumIntervalMs(int configured) {
  return std::clamp(configured, kM4MinimumIntervalMs, kM4MaximumIntervalMs);
}

static_assert(NormalizeMinimumIntervalMs(kM4DefaultMinimumIntervalMs) == 50);
static_assert(NormalizeMinimumIntervalMs(49) == 50);
static_assert(NormalizeMinimumIntervalMs(250) == 250);

// This is the retained engine's C layout, not ui::Rect. The observed normal
// ed060kc1 producer passes a zeroed instance rather than a 1448x1072 Rect.
struct EngineRect {
  std::int32_t left;
  std::int32_t top;
  std::int32_t right;
  std::int32_t bottom;
};

static_assert(sizeof(EngineRect) == 16);

using InitializeFn = int (*)();
using UpdateFn = int (*)(::android::sp<::android::GraphicBuffer>, unsigned char*, unsigned char*,
                         EngineRect*, int, int, unsigned char);

template <typename Function>
Function Resolve(void* handle, const char* name) {
  // bionic follows POSIX dlsym semantics. The targeted Android toolchain
  // supports this conversion for an ABI function pointer; the pointer never
  // crosses a generic/portable library boundary.
  return reinterpret_cast<Function>(dlsym(handle, name));
}

bool CanRead(const char* path) {
  // access(2) is not a faithful readiness check for an Android block device:
  // its credential-only check rejected the two Zhangyue backing partitions
  // despite their exact type/rule being live. A read-only open/close exercises
  // the same VFS/SELinux boundary that the retained driver needs, without
  // issuing an ioctl, mapping, or changing controller state.
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return false;
  }
  close(fd);
  return true;
}

bool CanReadWrite(const char* path) {
  return access(path, R_OK | W_OK) == 0;
}

bool CanWrite(const char* path) {
  return access(path, W_OK) == 0;
}

std::uint64_t ToNanoseconds(std::chrono::steady_clock::duration duration) {
  return static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

struct ObservedUpdateEnvelope {
  int mode;
  int flag1;
  unsigned char flag2;
};

std::optional<ObservedUpdateEnvelope> SelectObservedUpdateEnvelope(const Frame& frame) {
  if (frame.engine_mode == kObservedFullControlRecord) {
    return ObservedUpdateEnvelope{.mode = 2, .flag1 = 1, .flag2 = 1};
  }
  if (frame.engine_mode == kObservedNormalDifferentialRecord) {
    return ObservedUpdateEnvelope{.mode = 3, .flag1 = 0, .flag2 = 0};
  }
  return std::nullopt;
}

}  // namespace

class LowerEngineDirectBackend::Impl {
 public:
  std::mutex mutex;
  Diagnostics diagnostics;
  void* engine_handle = nullptr;
  InitializeFn initialize = nullptr;
  UpdateFn update = nullptr;
  SubmissionObserver completion_observer;
  std::chrono::steady_clock::time_point last_submission{};
};

LowerEngineDirectBackend::LowerEngineDirectBackend() : impl_(std::make_unique<Impl>()) {}

LowerEngineDirectBackend::~LowerEngineDirectBackend() = default;

bool LowerEngineDirectBackend::IsObservedFullInput(const Frame& frame) {
  // The record is intentionally expressed in the portable Frame as the
  // original stock policy word. This Android-14-only lower boundary permits
  // exactly the two recovered full-input record-to-tuple mappings.
  return frame.compose_buffer == nullptr && frame.source_buffer == nullptr &&
          frame.grayscale_buffer != nullptr && frame.dirty_rect.left == 0 &&
          frame.dirty_rect.top == 0 && frame.dirty_rect.right == kPanelWidth &&
          frame.dirty_rect.bottom == kPanelHeight && SelectObservedUpdateEnvelope(frame).has_value() &&
          frame.policy_flag == 0 && !frame.handwriting;
}

std::uint64_t LowerEngineDirectBackend::ActiveSubmissionLimit() {
  if (ContinuousModeEnabled()) return std::numeric_limits<std::uint64_t>::max();
  const int configured = ::android::base::GetIntProperty(
          kM4SubmissionLimitProperty, static_cast<int>(kM4DefaultSubmissionLimit));
  return static_cast<std::uint64_t>(
          std::clamp(configured, 1, static_cast<int>(kM4HardSubmissionLimit)));
}

bool LowerEngineDirectBackend::ContinuousModeEnabled() {
  // The temporary DSU presenter is meant to stay live while its demand gate is
  // being developed. Operators can still set this to 0 to restore the finite
  // diagnostic budget immediately.
  return ::android::base::GetBoolProperty(kM4ContinuousProperty, true);
}

bool LowerEngineDirectBackend::PresentationEnabled() {
  // R2 starts capture-only. An operator must deliberately arm the existing
  // volatile control after clean guest ADB evidence before this private engine
  // can initialize or accept a frame. R4 keeps a rootable diagnostic guest
  // convenient to inspect: only when the property is absent, a debuggable
  // build defaults armed. An explicit 0 remains a hard disarm, and a
  // production user build retains the former disarmed default.
  if (::android::base::GetProperty(kM4EnabledProperty, "").empty()) {
    return ::android::base::GetBoolProperty("ro.debuggable", false);
  }
  return ::android::base::GetBoolProperty(kM4EnabledProperty, false);
}

std::chrono::milliseconds LowerEngineDirectBackend::MinimumSubmissionInterval() {
  const int configured = ::android::base::GetIntProperty(kM4MinimumIntervalProperty,
                                                          kM4DefaultMinimumIntervalMs);
  return std::chrono::milliseconds(NormalizeMinimumIntervalMs(configured));
}

std::chrono::milliseconds LowerEngineDirectBackend::CompletionObservationInterval() {
  const int configured = ::android::base::GetIntProperty(kM4CompletionObservationProperty,
                                                          kM4DefaultCompletionObservationMs);
  return std::chrono::milliseconds(
          std::clamp(configured, kM4MinimumCompletionObservationMs,
                     kM4MaximumCompletionObservationMs));
}

bool LowerEngineDirectBackend::InitializeForOneShot() {
  std::lock_guard lock(impl_->mutex);
  if (impl_->diagnostics.state != State::kStopped) {
    return false;
  }

  std::uint32_t failures = kNoPreflightFailure;
  if (!CanRead(kDefaultWaveform) || !CanRead(kRegalWaveform) || !CanRead(kDefaultNm)) {
    failures |= kWaveformReadUnavailable;
  }
  if (!CanReadWrite(kSwtconDevice)) {
    failures |= kSwtconAccessUnavailable;
  }
  if (!CanReadWrite(kZhangyueDevice)) {
    failures |= kZhangyueAccessUnavailable;
  }
  if (!CanWrite(kPixFormat)) {
    failures |= kPixFormatWriteUnavailable;
  }
  if (!CanRead(kVcomValue)) {
    failures |= kVcomReadUnavailable;
  }
  if (!CanWrite(kTemperatureInfo)) {
    failures |= kTemperatureInfoWriteUnavailable;
  }
  if (!CanRead(kWaveformBlockDevice) || !CanRead(kTokenBlockDevice)) {
    failures |= kWaveformStorageReadUnavailable;
  }
  if (::android::base::GetProperty(kBridgeProperty, "") != "0") {
    failures |= kUnexpectedBridgeProperty;
  }
  impl_->diagnostics.preflight_failures = failures;
  if (failures != kNoPreflightFailure) {
    impl_->diagnostics.state = State::kPoisoned;
    ALOGE("M4 lower engine preflight rejected: failures=0x%x", failures);
    return false;
  }

  impl_->engine_handle = dlopen(kEngineLibrary, RTLD_NOW | RTLD_LOCAL);
  if (impl_->engine_handle == nullptr) {
    impl_->diagnostics.state = State::kPoisoned;
    ALOGE("M4 lower engine dlopen failed: %s", dlerror());
    return false;
  }
  impl_->initialize = Resolve<InitializeFn>(impl_->engine_handle, "iDisplayEngine_init");
  impl_->update = Resolve<UpdateFn>(impl_->engine_handle, "iDisplayEngine_update");
  if (impl_->initialize == nullptr || impl_->update == nullptr) {
    impl_->diagnostics.state = State::kPoisoned;
    ALOGE("M4 lower engine required-symbol resolution failed");
    return false;
  }
  impl_->diagnostics.state = State::kResolved;

  // The stock engine has no complete safe teardown. This may only be called
  // by a separately reviewed, one-owner output graft after all preflight
  // checks pass; current M1-M3 code never constructs or calls this backend.
  const int result = impl_->initialize();
  impl_->diagnostics.initializer_result = result;
  if (result != 0) {
    impl_->diagnostics.state = State::kPoisoned;
    ALOGE("M4 lower engine initialization failed: result=%d", result);
    return false;
  }
  impl_->diagnostics.state = State::kInitialized;
  ALOGI("M4 lower engine refresh controller initialized: continuous=%d active_limit=%llu "
        "finite_hard_limit=%llu minimum_interval_ms=%lld",
        ContinuousModeEnabled(),
        static_cast<unsigned long long>(ActiveSubmissionLimit()),
        static_cast<unsigned long long>(kM4HardSubmissionLimit),
        static_cast<long long>(MinimumSubmissionInterval().count()));
  return true;
}

bool LowerEngineDirectBackend::Submit(const Frame& frame) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->diagnostics.state != State::kInitialized || !IsObservedFullInput(frame)) {
    ++impl_->diagnostics.rejected_frames;
    return false;
  }
  // Re-read the volatile property here, immediately before the retained
  // engine call. This protects against an already-converted frame reaching
  // the serial worker after the capture seam was disarmed.
  if (!PresentationEnabled()) {
    ++impl_->diagnostics.disarmed_frames;
    return false;
  }

  // The retained engine remains the stock repeated-frame owner. By default
  // M4 normally remains continuous while the demand gate is being developed.
  // An operator may set debug.neo2.eink.continuous=0 in this temporary guest
  // to restore the finite 24-submission diagnostic budget immediately. The
  // 50-ms--5-second output-rate clamp remains
  // in force in both modes. It is a queue-attempt control, not a claimed
  // physical-completion signal.
  if (!ContinuousModeEnabled() && impl_->diagnostics.submissions >= kM4HardSubmissionLimit) {
    impl_->diagnostics.state = State::kSubmitted;
    ++impl_->diagnostics.rejected_frames;
    return false;
  }
  if (impl_->diagnostics.submissions >= ActiveSubmissionLimit()) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  const auto minimum_interval = MinimumSubmissionInterval();
  if (impl_->diagnostics.submissions != 0 &&
      now - impl_->last_submission < minimum_interval) {
    ++impl_->diagnostics.rejected_frames;
    return false;
  }

  const auto envelope = SelectObservedUpdateEnvelope(frame);
  if (!envelope) {
    ++impl_->diagnostics.rejected_frames;
    return false;
  }
  const bool differential = frame.engine_mode == kObservedNormalDifferentialRecord;

  // The captured stock normal and mode-3 cursor calls establish null
  // sp<GraphicBuffer> and optional-source arguments, owned M3 gray bytes,
  // and a zeroed RECT. This same-build route matches that wire shape; it does
  // not claim stock's surrounding controller lifecycle or engine state.
  // The engine copies the gray bytes before this call returns and owns all
  // later waveform and DRM transport work. R3 intentionally does not invoke
  // raw iDisplayEngine_display_is_idle(): R2 proved that query crashes when
  // detached from stock's Sync-wrapper lifecycle. The stock ordering remains
  // an open B002 qualification requirement, not a condition this lower-level
  // evidence run claims to reproduce.
  ::android::sp<::android::GraphicBuffer> compose_handle;
  EngineRect dirty_rect{};
  const int result = impl_->update(compose_handle, nullptr,
                                   const_cast<unsigned char*>(frame.grayscale_buffer->Data()),
                                   &dirty_rect, envelope->mode, envelope->flag1, envelope->flag2);
  impl_->diagnostics.update_result = result;
  ++impl_->diagnostics.submissions;
  if (differential) {
    ++impl_->diagnostics.differential_submissions;
  } else {
    ++impl_->diagnostics.full_control_submissions;
  }
  if (result != kObservedUpdateAcceptedResult) {
    if (differential) {
      ++impl_->diagnostics.differential_failures;
    }
    impl_->diagnostics.state = State::kPoisoned;
    ALOGE("M4 lower engine update failed: mode=%d result=%d", envelope->mode, result);
    return false;
  }
  impl_->last_submission = now;
  impl_->completion_observer.RecordAccepted(frame.sequence, ToNanoseconds(now.time_since_epoch()));
  const auto completion_diagnostics = impl_->completion_observer.diagnostics();
  impl_->diagnostics.completion_unknown_submissions =
          completion_diagnostics.completion_unknown_submissions;
  impl_->diagnostics.completion_timeout_observations =
          completion_diagnostics.timeout_observations;
  impl_->diagnostics.pending_submission_id = completion_diagnostics.latest_submission_id;
  impl_->diagnostics.pending_timeout_elapsed_ns = completion_diagnostics.latest_timeout_elapsed_ns;
  impl_->diagnostics.pending_completion_unknown = completion_diagnostics.pending_completion_unknown;
  if (!ContinuousModeEnabled() && impl_->diagnostics.submissions == kM4HardSubmissionLimit) {
    impl_->diagnostics.state = State::kSubmitted;
  }
  ALOGI("M4 lower engine refresh update accepted: sequence=%llu mode=%d submission=%llu continuous=%d "
        "active_limit=%llu finite_hard_limit=%llu interval_ms=%lld",
        static_cast<unsigned long long>(frame.sequence),
        envelope->mode,
        static_cast<unsigned long long>(impl_->diagnostics.submissions),
        ContinuousModeEnabled(),
        static_cast<unsigned long long>(ActiveSubmissionLimit()),
        static_cast<unsigned long long>(kM4HardSubmissionLimit),
        static_cast<long long>(minimum_interval.count()));
  return true;
}

void LowerEngineDirectBackend::ObserveCompletionTimeout() {
  std::lock_guard lock(impl_->mutex);
  const auto now = std::chrono::steady_clock::now();
  const auto timeout = CompletionObservationInterval();
  if (!impl_->completion_observer.ObserveTimeout(
              ToNanoseconds(now.time_since_epoch()), ToNanoseconds(timeout))) {
    return;
  }
  const auto completion_diagnostics = impl_->completion_observer.diagnostics();
  impl_->diagnostics.completion_timeout_observations =
          completion_diagnostics.timeout_observations;
  impl_->diagnostics.pending_timeout_elapsed_ns =
          completion_diagnostics.latest_timeout_elapsed_ns;
  ALOGI("M4 panel completion remains unknown: submission=%llu elapsed_ns=%llu observation_ms=%lld",
        static_cast<unsigned long long>(completion_diagnostics.latest_submission_id),
        static_cast<unsigned long long>(completion_diagnostics.latest_timeout_elapsed_ns),
        static_cast<long long>(timeout.count()));
}

LowerEngineDirectBackend::Diagnostics LowerEngineDirectBackend::diagnostics() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->diagnostics;
}

}  // namespace neo2::eink::android
