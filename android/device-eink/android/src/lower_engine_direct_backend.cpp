#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "Neo2EinkLowerEngine"

#include "neo2/eink/lower_engine_direct_backend.h"
#include "neo2/eink/android_eink_pipeline.h"
#include "neo2/eink/ocean_waveform_compatibility.h"

#include <eink_device_profile.h>
#include <eink_color_quality.h>
#include <eink_engine_marker_guard.h>
#include <atomic>
#include <eink_lower_engine_layout.h>
#include <eink_lower_engine_plane_contract.h>

#include <android-base/properties.h>
#include <android-base/file.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <log/log.h>
#include <ui/GraphicBuffer.h>
#include <ui/PixelFormat.h>
#include <ui/Rect.h>
#include <utils/StrongPointer.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <thread>
#include <vector>
#include <unistd.h>

namespace neo2::eink::android {

namespace {

std::atomic<std::uint64_t> g_quality_config{0};
std::atomic<std::uint64_t> g_vivid_config{0};
std::atomic<std::uint64_t> g_gray_config{static_cast<std::uint64_t>(GrayPreference::kBalanced)};

constexpr const auto& kDeviceProfile = BuildEinkDeviceProfile();
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
// Marker progress replaces guessed dwell for color-quality pairs. Optional
// debug gaps remain for comparison; the production default adds no fixed delay.
// The existing submission floor still applies. Neither is a completion fence.
constexpr int kColorQualityGuardMs = 0;
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
constexpr char kAuraCPixFormat[] = "/dev/pix_fmt";
constexpr char kVcomValue[] = "/sys/eink/app/vcomvalue";
constexpr char kTemperatureInfo[] = "/dev/einktemperinfo";
constexpr char kBridgeProperty[] = "ro.eink.fpga.bridge.support";
constexpr char kColorRuntime[] = "/system/lib64/libEink_Kaleido_render_64.so";
constexpr char kColorUtilities[] = "/system/lib64/libeinkutils.so";
constexpr char kColorCfaLut[] = "/vendor/etc/Kaleido_CFA_LUT.lut";
constexpr char kColorAieLut[] = "/vendor/etc/AIE_S4.aie";
constexpr char kColorVpcoaLut[] = "/vendor/etc/VPCOA_LUT.lut";
constexpr char kColorCfaProperty[] = "ro.sys.eink.idisplay.cfa";

constexpr const char* PixFormatPath() {
  if constexpr (kDeviceProfile.variant == EinkDeviceVariant::kAuraC) {
    return kAuraCPixFormat;
  } else {
    return kPixFormat;
  }
}

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
using EngineMarkerFn = std::uint32_t (*)();
using RegalSupportFn = int (*)(int*, int*);
using KaleidoInitializeFn = int (*)(unsigned char*, int, int, int);
using KaleidoLoadLutFn = int (*)(unsigned char*, int);
using KaleidoMapVpcoaFn = int (*)(unsigned char*, int, int, int, unsigned char*, int,
                                  int, int, int, int, int, int);
using AuraGrayFn = void (*)(void*, int, const ::android::Rect&, void*, int, int, int);
using MonochromeUpdateFn = int (*)(::android::sp<::android::GraphicBuffer>, unsigned char*,
                                   unsigned char*, EngineRect*, int, int, unsigned char);
// Aura stock libeinksfpatch.so calls this export with exactly six arguments:
// mapped RGBA bytes, owned gray bytes, RECT, waveform mode, and two flags. Its
// pre_process_update_image(unsigned char*, ...) immediately copies the first
// plane, so an sp<GraphicBuffer> is not ABI-compatible with this color path.
using AuraColorUpdateFn = int (*)(unsigned char*, unsigned char*, EngineRect*, int, int,
                                  unsigned char);
using UpdateFn = std::conditional_t<kDeviceProfile.variant == EinkDeviceVariant::kAuraC,
                                    AuraColorUpdateFn, MonochromeUpdateFn>;

[[maybe_unused]] int InvokeUpdate(
        AuraColorUpdateFn update,
        const ::android::sp<::android::GraphicBuffer>& /* compose_handle */,
        unsigned char* mapped_rgba, unsigned char* gray, EngineRect* dirty_rect, int mode,
        int flag1, unsigned char flag2) {
  return update(mapped_rgba, gray, dirty_rect, mode, flag1, flag2);
}

[[maybe_unused]] int InvokeUpdate(
        MonochromeUpdateFn update,
        const ::android::sp<::android::GraphicBuffer>& compose_handle,
        unsigned char* /* mapped_rgba */, unsigned char* gray, EngineRect* dirty_rect, int mode,
        int flag1, unsigned char flag2) {
  return update(compose_handle, nullptr, gray, dirty_rect, mode, flag1, flag2);
}

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

bool ReadBytes(const char* path, std::string* bytes) {
  return ::android::base::ReadFileToString(path, bytes) && !bytes->empty() &&
          bytes->size() <= static_cast<std::size_t>(std::numeric_limits<int>::max());
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

bool GetColorQualityEnabled() { return (g_quality_config.load() & 1) != 0; }
void SetColorQualityEnabled(bool enabled) {
  if constexpr (!kDeviceProfile.has_color_cfa) return;
  auto old = g_quality_config.load();
  while ((old & 1) != static_cast<std::uint64_t>(enabled) &&
         !g_quality_config.compare_exchange_weak(old, ((old + 2) & ~1ULL) | enabled)) {}
}

bool GetVividColorEnabled() { return (g_vivid_config.load() & 1) != 0; }
void SetVividColorEnabled(bool enabled) {
  if constexpr (!kDeviceProfile.has_color_cfa) return;
  auto old = g_vivid_config.load();
  while ((old & 1) != static_cast<std::uint64_t>(enabled) &&
         !g_vivid_config.compare_exchange_weak(old, ((old + 2) & ~1ULL) | enabled)) {}
}

GrayPreference GetGrayPreference() {
  return static_cast<GrayPreference>(g_gray_config.load() & 3);
}
void SetGrayPreference(GrayPreference preference) {
  if constexpr (!kDeviceProfile.has_color_cfa) return;
  const auto value = static_cast<std::uint64_t>(preference);
  if (!IsGrayPreference(static_cast<int>(value))) return;
  auto old = g_gray_config.load();
  while ((old & 3) != value &&
         !g_gray_config.compare_exchange_weak(old, ((old + 4) & ~3ULL) | value)) {}
}

class LowerEngineDirectBackend::Impl {
 public:
  std::mutex mutex;
  Diagnostics diagnostics;
  void* engine_handle = nullptr;
  void* color_handle = nullptr;
  void* color_utilities_handle = nullptr;
  InitializeFn initialize = nullptr;
  RegalSupportFn regal_support = nullptr;
  UpdateFn update = nullptr;
  EngineMarkerFn submitted_marker = nullptr;
  EngineMarkerFn displaying_marker = nullptr;
  KaleidoInitializeFn kaleido_initialize = nullptr;
  KaleidoLoadLutFn kaleido_load_aie = nullptr;
  KaleidoLoadLutFn kaleido_load_vpcoa = nullptr;
  KaleidoMapVpcoaFn kaleido_map_vpcoa = nullptr;
  AuraGrayFn aura_gray = nullptr;
  SubmissionObserver completion_observer;
  std::chrono::steady_clock::time_point last_submission{};
  std::vector<std::uint8_t> arranged_grayscale;
  // The Kaleido runtime retains these buffers after its initialization calls
  // and reads them later from its asynchronous mapping worker. Keep the exact
  // verified LUT bytes alive for the lifetime of the backend.
  std::string color_cfa_lut;
  std::string color_aie_lut;
  std::string color_vpcoa_lut;
  ColorQualityHistory quality_history;
  std::uint64_t quality_config = 0;
  std::uint64_t gray_config = 0;
  std::uint64_t vivid_config = 0;
  std::vector<std::uint8_t> white_input;
  std::vector<std::uint8_t> color_input;
  std::vector<std::uint8_t> color_output;
  std::vector<std::uint8_t> color_grayscale;

  bool WaitForMarkerStart(std::uint32_t before, std::uint64_t sequence,
                          const char* phase,
                          std::chrono::steady_clock::time_point accepted_at) {
    for (;;) {
      const auto submitted = submitted_marker();
      const auto displaying = displaying_marker();
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - accepted_at).count();
      const auto progress = CheckEngineMarkerStart(before, submitted, displaying,
              static_cast<std::uint64_t>(elapsed), 5000);
      if (progress != EngineMarkerProgress::kWaiting) {
        ALOGI("quality_marker sequence=%llu phase=%s before=%u submitted=%u "
              "displaying=%u elapsed_ms=%lld progress=%d completion=unknown",
              static_cast<unsigned long long>(sequence), phase, before, submitted,
              displaying, static_cast<long long>(elapsed), static_cast<int>(progress));
        if (progress == EngineMarkerProgress::kStarted) return true;
        diagnostics.state = State::kPoisoned;
        quality_history.Reset();
        ALOGE("Aura C quality marker guard stopped uncertain sequence; no retry");
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
};

LowerEngineDirectBackend::LowerEngineDirectBackend() : impl_(std::make_unique<Impl>()) {}

LowerEngineDirectBackend::~LowerEngineDirectBackend() = default;

bool LowerEngineDirectBackend::IsObservedFullInput(const Frame& frame) {
  // The record is intentionally expressed in the portable Frame as the
  // original stock policy word. This Android-14-only lower boundary permits
  // exactly the two recovered full-input record-to-tuple mappings.
  return ValidateLowerEnginePlanes(kDeviceProfile, frame) == LowerEnginePlaneStatus::kReady &&
          frame.dirty_rect.left == 0 &&
          frame.dirty_rect.top == 0 &&
          frame.dirty_rect.right == static_cast<int>(kDeviceProfile.panel_width) &&
          frame.dirty_rect.bottom == static_cast<int>(kDeviceProfile.panel_height) &&
          SelectObservedUpdateEnvelope(frame).has_value() &&
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
  // Continuous output requires a separately authorized supervised session.
  // Default to the finite public bring-up budget.
  return ::android::base::GetBoolProperty(kM4ContinuousProperty, false);
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
  if (!CanRead(kDefaultWaveform) || !CanRead(kRegalWaveform)) {
    failures |= kWaveformReadUnavailable;
  }
  if constexpr (kDeviceProfile.has_default_nm_resource) {
    if (!CanRead(kDefaultNm)) {
      failures |= kWaveformReadUnavailable;
    }
  }
  if (!CanReadWrite(kSwtconDevice)) {
    failures |= kSwtconAccessUnavailable;
  }
  if (!CanReadWrite(kZhangyueDevice)) {
    failures |= kZhangyueAccessUnavailable;
  }
  if (!CanWrite(PixFormatPath())) {
    failures |= kPixFormatWriteUnavailable;
  }
  if (!CanRead(kVcomValue)) {
    failures |= kVcomReadUnavailable;
  }
  if (!CanWrite(kTemperatureInfo)) {
    failures |= kTemperatureInfoWriteUnavailable;
  }
  if (!CanRead(kDeviceProfile.waveform_block_device.data()) ||
      !CanRead(kDeviceProfile.token_block_device.data())) {
    failures |= kWaveformStorageReadUnavailable;
  }
  if (::android::base::GetProperty(kBridgeProperty, "") != "0") {
    failures |= kUnexpectedBridgeProperty;
  }
  if constexpr (kDeviceProfile.has_color_cfa) {
    if (!CanRead(kColorRuntime) || !CanRead(kColorUtilities)) {
      failures |= kColorRuntimeUnavailable;
    }
    if (!CanRead(kColorCfaLut) || !CanRead(kColorAieLut) || !CanRead(kColorVpcoaLut) ||
        ::android::base::GetProperty(kColorCfaProperty, "") != "1") {
      failures |= kColorConfigurationUnavailable;
    }
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

  if constexpr (kDeviceProfile.has_color_cfa) {
    // Read-only exports inspected in the retained Aura C engine. The display
    // marker is written before display_frame_show_each, not at completion.
    impl_->submitted_marker = Resolve<EngineMarkerFn>(impl_->engine_handle,
            "_Z39pre_process_get_new_image_update_markerv");
    impl_->displaying_marker = Resolve<EngineMarkerFn>(impl_->engine_handle,
            "display_frame_get_idisplay_update_marker");
    if (impl_->submitted_marker == nullptr || impl_->displaying_marker == nullptr) {
      impl_->diagnostics.state = State::kPoisoned;
      ALOGE("Aura C engine progress symbol resolution failed");
      return false;
    }
    impl_->regal_support = Resolve<RegalSupportFn>(
            impl_->engine_handle, "iDisplayEngine_eink_regal_support");
    impl_->color_handle = dlopen(kColorRuntime, RTLD_NOW | RTLD_LOCAL);
    impl_->color_utilities_handle = dlopen(kColorUtilities, RTLD_NOW | RTLD_LOCAL);
    if (impl_->regal_support == nullptr || impl_->color_handle == nullptr ||
        impl_->color_utilities_handle == nullptr) {
      impl_->diagnostics.state = State::kPoisoned;
      ALOGE("Aura C color runtime load failed: %s", dlerror());
      return false;
    }
    impl_->kaleido_initialize = Resolve<KaleidoInitializeFn>(
            impl_->color_handle, "Eink_Kaleido_render_Init");
    impl_->kaleido_load_aie =
            Resolve<KaleidoLoadLutFn>(impl_->color_handle, "Eink_Load_AIE_LUT");
    impl_->kaleido_load_vpcoa =
            Resolve<KaleidoLoadLutFn>(impl_->color_handle, "Eink_Load_VPCOA_LUT");
    impl_->kaleido_map_vpcoa = Resolve<KaleidoMapVpcoaFn>(
            impl_->color_handle, "Eink_color_mapping_VPCOA");
    impl_->aura_gray = Resolve<AuraGrayFn>(
            impl_->color_utilities_handle,
            "_ZN7android34eink_rgba8888_to_gray_for_t1000_hwEPviRKNS_4RectES0_iii");
    if (impl_->kaleido_initialize == nullptr || impl_->kaleido_load_aie == nullptr ||
        impl_->kaleido_load_vpcoa == nullptr || impl_->kaleido_map_vpcoa == nullptr ||
        impl_->aura_gray == nullptr) {
      impl_->diagnostics.state = State::kPoisoned;
      ALOGE("Aura C color runtime symbol resolution failed");
      return false;
    }
  }

  if constexpr (kDeviceProfile.variant == EinkDeviceVariant::kOcean) {
    const auto compatibility =
            InstallOceanWaveformScalarStrncpyCompatibility(impl_->engine_handle);
    impl_->diagnostics.waveform_copy_compatibility_result =
            static_cast<std::uint8_t>(compatibility);
    if (compatibility != OceanWaveformCompatibilityResult::kApplied) {
      impl_->diagnostics.state = State::kPoisoned;
      ALOGE("Ocean waveform scalar-copy compatibility failed: result=%u",
            static_cast<unsigned int>(compatibility));
      return false;
    }
    impl_->diagnostics.waveform_copy_compatibility_applied = true;
    ALOGI("Ocean waveform scalar-copy compatibility applied");
  }

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
  if constexpr (kDeviceProfile.has_color_cfa) {
    if (!ReadBytes(kColorCfaLut, &impl_->color_cfa_lut) ||
        !ReadBytes(kColorAieLut, &impl_->color_aie_lut) ||
        !ReadBytes(kColorVpcoaLut, &impl_->color_vpcoa_lut)) {
      impl_->diagnostics.state = State::kPoisoned;
      ALOGE("Aura C color LUT read failed after engine initialization");
      return false;
    }
    const int color_init_result = impl_->kaleido_initialize(
            reinterpret_cast<unsigned char*>(impl_->color_cfa_lut.data()),
            static_cast<int>(impl_->color_cfa_lut.size()),
            static_cast<int>(kDeviceProfile.panel_width),
            static_cast<int>(kDeviceProfile.panel_height));
    const int aie_result = color_init_result == 0
            ? impl_->kaleido_load_aie(
                      reinterpret_cast<unsigned char*>(impl_->color_aie_lut.data()),
                      static_cast<int>(impl_->color_aie_lut.size()))
            : -1;
    const int vpcoa_result = aie_result == 0
            ? impl_->kaleido_load_vpcoa(
                      reinterpret_cast<unsigned char*>(impl_->color_vpcoa_lut.data()),
                      static_cast<int>(impl_->color_vpcoa_lut.size()))
            : -1;
    int regal_result = 1;
    int monochrome_result = 1;
    const int support_result = vpcoa_result == 0
            ? impl_->regal_support(&regal_result, &monochrome_result)
            : -1;
    impl_->diagnostics.color_initializer_result = color_init_result;
    impl_->diagnostics.color_aie_result = aie_result;
    impl_->diagnostics.color_vpcoa_result = vpcoa_result;
    impl_->diagnostics.regal_support_result = support_result;
    impl_->diagnostics.regal_initializer_result = regal_result;
    impl_->diagnostics.monochrome_initializer_result = monochrome_result;
    if (color_init_result != 0 || aie_result != 0 || vpcoa_result != 0 ||
        support_result != 0) {
      impl_->diagnostics.state = State::kPoisoned;
      ALOGE("Aura C color initialization failed: cfa=%d aie=%d vpcoa=%d support=%d "
            "regal=%d mono=%d", color_init_result, aie_result, vpcoa_result,
            support_result, regal_result, monochrome_result);
      return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(kDeviceProfile.panel_width) *
            static_cast<std::size_t>(kDeviceProfile.panel_height);
    impl_->color_input.resize(pixels * 4);
    impl_->color_output.resize(pixels * 4);
    impl_->color_grayscale.resize(pixels);
    ALOGI("Aura C color path initialized: mapping=VPCOA increment=8 regal=%d mono=%d",
          regal_result, monochrome_result);
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

  const auto quality_started = std::chrono::steady_clock::now();
  const auto quality_config = g_quality_config.load();
  const auto gray_config = g_gray_config.load();
  const auto vivid_config = g_vivid_config.load();
  if (quality_config != impl_->quality_config || gray_config != impl_->gray_config ||
      vivid_config != impl_->vivid_config || frame.sleep_image_epoch != 0) {
    impl_->quality_history.Reset();
    impl_->quality_config = quality_config;
    impl_->gray_config = gray_config;
    impl_->vivid_config = vivid_config;
  }
  const bool quality_enabled = (quality_config & 1) != 0;
  bool white_submitted = false;
  std::uint32_t white_marker = 0;
  bool automatic_quality_pair = false;
  std::chrono::milliseconds quality_before{0}, quality_between{0}, quality_after{0};
  const auto previous_submission = impl_->last_submission;
  auto white_call_started = previous_submission;
  auto white_call_returned = previous_submission;
  std::size_t whitened_pixels = 0;
  Frame selected_frame = frame;
  if (frame.diagnostic_request != 0) {
    if constexpr (!kDeviceProfile.has_color_cfa) return false;
    if (frame.sleep_image_epoch != 0) return false;
    selected_frame.engine_mode = frame.diagnostic_refresh == DiagnosticRefresh::kFull
            ? kObservedFullControlRecord : kObservedNormalDifferentialRecord;
  }
  const auto envelope = SelectObservedUpdateEnvelope(selected_frame);
  if (!envelope) {
    ++impl_->diagnostics.rejected_frames;
    return false;
  }
  const bool differential = selected_frame.engine_mode == kObservedNormalDifferentialRecord;

  // Monochrome devices retain the recovered null GraphicBuffer plus owned-gray
  // call shape. Aura C retains the pool-owned RGBA GraphicBuffer and the gray
  // plane derived from that exact snapshot together through this call. The
  // engine copies the bytes before this call returns and owns all later
  // waveform and DRM transport work. R3 intentionally does not invoke
  // raw iDisplayEngine_display_is_idle(): R2 proved that query crashes when
  // detached from stock's Sync-wrapper lifecycle. The stock ordering remains
  // an open B002 qualification requirement, not a condition this lower-level
  // evidence run claims to reproduce.
  const std::uint8_t* engine_grayscale = frame.grayscale_buffer->Data();
  if constexpr (kDeviceProfile.lower_engine_gray_layout ==
                LowerEngineGrayLayout::kAdjacentRowPairsInterleaved) {
    if (!InterleaveAdjacentGrayscaleRows(
                engine_grayscale, frame.grayscale_buffer->ByteCount(),
                static_cast<int>(kDeviceProfile.panel_width),
                static_cast<int>(kDeviceProfile.panel_height), &impl_->arranged_grayscale)) {
      ++impl_->diagnostics.rejected_frames;
      ALOGE("M4 lower engine gray-layout conversion failed");
      return false;
    }
    engine_grayscale = impl_->arranged_grayscale.data();
  }

  ::android::sp<::android::GraphicBuffer> compose_handle;
  unsigned char* mapped_rgba = nullptr;
  if constexpr (kDeviceProfile.has_color_cfa) {
    if (frame.compose_buffer == nullptr) {
      if (frame.sleep_image_rgba) {
        if (frame.sleep_image_epoch == 0 ||
            frame.sleep_image_rgba->size() != impl_->color_input.size()) {
          ++impl_->diagnostics.rejected_frames;
          return false;
        }
        impl_->color_input.assign(frame.sleep_image_rgba->begin(), frame.sleep_image_rgba->end());
      } else if (!ExpandSleepGrayscaleToRgba(frame, &impl_->color_input)) {
        ++impl_->diagnostics.rejected_frames;
        return false;
      }
    } else {
      if (frame.compose_buffer->TypeTag() != AndroidComposeBuffer::kTypeTag) {
        ++impl_->diagnostics.rejected_frames;
        ALOGE("Aura C lower engine rejected an untyped color plane");
        return false;
      }
      const auto color = std::static_pointer_cast<AndroidComposeBuffer>(frame.compose_buffer);
      compose_handle = color->buffer();
      if (!color->owns_snapshot_lease() || compose_handle == nullptr ||
          compose_handle->getWidth() != kDeviceProfile.panel_width ||
          compose_handle->getHeight() != kDeviceProfile.panel_height ||
          compose_handle->getPixelFormat() != ::android::PIXEL_FORMAT_RGBA_8888) {
        ++impl_->diagnostics.rejected_frames;
        ALOGE("Aura C lower engine rejected invalid or unowned RGBA plane");
        return false;
      }
      void* address = nullptr;
      int32_t bytes_per_pixel = -1;
      int32_t bytes_per_stride = -1;
      const auto lock_status = compose_handle->lockAsync(
              ::android::GraphicBuffer::USAGE_SW_READ_OFTEN, &address, -1, &bytes_per_pixel,
              &bytes_per_stride);
      if (lock_status != ::android::NO_ERROR || address == nullptr || bytes_per_pixel != 4 ||
          bytes_per_stride < static_cast<int32_t>(kDeviceProfile.panel_width) * bytes_per_pixel) {
        if (lock_status == ::android::NO_ERROR && address != nullptr) {
          static_cast<void>(compose_handle->unlock());
        }
        ++impl_->diagnostics.rejected_frames;
        ALOGE("Aura C lower engine could not map its owned RGBA plane: status=%d bpp=%d stride=%d",
              lock_status, bytes_per_pixel, bytes_per_stride);
        return false;
      }
      const auto row_bytes = static_cast<std::size_t>(kDeviceProfile.panel_width) * 4;
      for (std::uint32_t row = 0; row < kDeviceProfile.panel_height; ++row) {
        std::memcpy(impl_->color_input.data() + static_cast<std::size_t>(row) * row_bytes,
                    static_cast<unsigned char*>(address) +
                            static_cast<std::size_t>(row) *
                                    static_cast<std::size_t>(bytes_per_stride),
                    row_bytes);
      }
    }
    ApplyGrayPreference(impl_->color_input, static_cast<GrayPreference>(gray_config & 3));
    if ((vivid_config & 1) != 0) ApplyVividColor(impl_->color_input);
    if (frame.diagnostic_refresh == DiagnosticRefresh::kWhiteThenTarget) {
      impl_->white_input.assign(impl_->color_input.size(), 0xff);
      whitened_pixels = impl_->color_input.size() / 4;
    } else if (frame.sleep_image_epoch == 0 &&
               (frame.diagnostic_refresh == DiagnosticRefresh::kColorCleanup ||
                (quality_enabled && frame.diagnostic_request == 0 && differential))) {
      whitened_pixels = impl_->quality_history.Prepare(impl_->color_input, impl_->white_input,
              {.chroma = ::android::base::GetIntProperty("debug.neo2.eink.quality_chroma", 24),
               .change = ::android::base::GetIntProperty("debug.neo2.eink.quality_delta", 12),
               .near_white = ::android::base::GetIntProperty("debug.neo2.eink.quality_white", 232)},
              frame.diagnostic_refresh == DiagnosticRefresh::kColorCleanup);
    }
    if (whitened_pixels != 0) {
      automatic_quality_pair = quality_enabled && frame.diagnostic_request == 0 &&
              frame.diagnostic_refresh == DiagnosticRefresh::kNone &&
              frame.sleep_image_epoch == 0 && differential;
      if (automatic_quality_pair) {
        const bool debuggable = ::android::base::GetBoolProperty("ro.debuggable", false);
        const auto gap = [debuggable](const char* property) {
          // Explicit zero remains available for diagnostic A/B reversals.
          const int configured = debuggable
                  ? ::android::base::GetIntProperty(property, kColorQualityGuardMs)
                  : kColorQualityGuardMs;
          return std::chrono::milliseconds(std::clamp(configured, 0, 1000));
        };
        quality_before = gap("debug.neo2.eink.quality_before_ms");
        quality_between = gap("debug.neo2.eink.quality_between_ms");
        quality_after = gap("debug.neo2.eink.quality_after_ms");
      }
      // Keep white, its gap, target, and its settling guard inside the same
      // serial worker operation. Incoming frames may coalesce upstream but
      // cannot submit between these phases or during the post-target guard.
      if (impl_->diagnostics.submissions + 2 > ActiveSubmissionLimit()) {
        if (compose_handle != nullptr) static_cast<void>(compose_handle->unlock());
        return false;
      }
      const int white_mapping = impl_->kaleido_map_vpcoa(
              impl_->white_input.data(), 1,
              static_cast<int>(kDeviceProfile.panel_width),
              static_cast<int>(kDeviceProfile.panel_height), impl_->color_output.data(), 1, 8,
              0, 0, static_cast<int>(kDeviceProfile.panel_width),
              static_cast<int>(kDeviceProfile.panel_height), 0);
      if (white_mapping != 0) {
        if (compose_handle != nullptr) static_cast<void>(compose_handle->unlock());
        impl_->diagnostics.state = State::kPoisoned;
        return false;
      }
      const ::android::Rect white_rect(0, 0, static_cast<int>(kDeviceProfile.panel_width),
                                      static_cast<int>(kDeviceProfile.panel_height));
      impl_->aura_gray(impl_->color_output.data(), static_cast<int>(kDeviceProfile.panel_width),
                      white_rect, impl_->color_grayscale.data(),
                      static_cast<int>(kDeviceProfile.panel_width), 0, 0);
      EngineRect white_dirty{};
      // Minimum gaps are measured from call return, not physical completion.
      if (automatic_quality_pair && impl_->diagnostics.submissions != 0)
        std::this_thread::sleep_until(previous_submission + quality_before);
      white_call_started = std::chrono::steady_clock::now();
      const auto before_white_marker = impl_->submitted_marker();
      if (!CanTrackNextEngineMarker(before_white_marker)) {
        if (compose_handle != nullptr) static_cast<void>(compose_handle->unlock());
        impl_->diagnostics.state = State::kPoisoned;
        return false;
      }
      const int white_result = InvokeUpdate(impl_->update, compose_handle,
              impl_->color_output.data(), impl_->color_grayscale.data(), &white_dirty, 3, 0, 0);
      ++impl_->diagnostics.submissions;
      ++impl_->diagnostics.differential_submissions;
      impl_->last_submission = std::chrono::steady_clock::now();
      white_call_returned = impl_->last_submission;
      if (white_result == kObservedUpdateAcceptedResult) {
        white_submitted = true;
        impl_->completion_observer.RecordAccepted(frame.sequence,
                ToNanoseconds(impl_->last_submission.time_since_epoch()));
      }
      ALOGI("diagnostic_refresh id=%llu phase=white mode=3 result=%d completion=unknown",
            static_cast<unsigned long long>(frame.diagnostic_request), white_result);
      if (white_result != kObservedUpdateAcceptedResult) {
        if (compose_handle != nullptr) static_cast<void>(compose_handle->unlock());
        impl_->diagnostics.state = State::kPoisoned;
        impl_->diagnostics.update_result = white_result;
        ++impl_->diagnostics.differential_failures;
        return false; // Never retry an uncertain partially submitted sequence.
      }
      white_marker = before_white_marker + 1;
    }
    const int mapping_result = impl_->kaleido_map_vpcoa(
            impl_->color_input.data(), 1, static_cast<int>(kDeviceProfile.panel_width),
            static_cast<int>(kDeviceProfile.panel_height), impl_->color_output.data(), 1, 8,
            0, 0, static_cast<int>(kDeviceProfile.panel_width),
            static_cast<int>(kDeviceProfile.panel_height), 0);
    if (mapping_result != 0) {
      if (compose_handle != nullptr) static_cast<void>(compose_handle->unlock());
      ++impl_->diagnostics.rejected_frames;
      impl_->diagnostics.state = State::kPoisoned;
      ALOGE("Aura C VPCOA mapping failed: result=%d", mapping_result);
      return false;
    }
    const ::android::Rect full_rect(0, 0, static_cast<int32_t>(kDeviceProfile.panel_width),
                                    static_cast<int32_t>(kDeviceProfile.panel_height));
    impl_->aura_gray(impl_->color_output.data(), static_cast<int>(kDeviceProfile.panel_width),
                     full_rect, impl_->color_grayscale.data(),
                     static_cast<int>(kDeviceProfile.panel_width), 0, 0);
    mapped_rgba = impl_->color_output.data();
    engine_grayscale = impl_->color_grayscale.data();
  }
  EngineRect dirty_rect{};
  if (white_submitted) {
    // Target mapping overlaps the gap. This is a minimum return-to-call
    // spacing, not an extra dwell added after mapping.
    std::this_thread::sleep_until(impl_->last_submission +
            std::max(MinimumSubmissionInterval(), quality_between));
    // Target mapping overlaps white's progress. Withhold only its submission
    // while white can still be merged away; incoming frames coalesce upstream.
    if (!impl_->WaitForMarkerStart(white_marker - 1, frame.sequence, "white",
                                  white_call_returned)) {
      if (compose_handle != nullptr) static_cast<void>(compose_handle->unlock());
      return false;
    }
  }
  const auto target_call_started = std::chrono::steady_clock::now();
  const auto before_target_marker = white_submitted ? impl_->submitted_marker() : 0;
  if (white_submitted && (before_target_marker != white_marker ||
                         !CanTrackNextEngineMarker(before_target_marker))) {
    if (compose_handle != nullptr) static_cast<void>(compose_handle->unlock());
    impl_->diagnostics.state = State::kPoisoned;
    impl_->quality_history.Reset();
    return false;
  }
  const int result = InvokeUpdate(impl_->update, compose_handle, mapped_rgba,
                                  const_cast<unsigned char*>(engine_grayscale), &dirty_rect,
                                  envelope->mode, envelope->flag1, envelope->flag2);
  const auto target_call_returned = std::chrono::steady_clock::now();
  if constexpr (kDeviceProfile.variant == EinkDeviceVariant::kAuraC) {
    if (compose_handle != nullptr) static_cast<void>(compose_handle->unlock());
  }
  if (frame.diagnostic_request != 0) {
    ALOGI("diagnostic_refresh id=%llu phase=target mode=%d result=%d completion=unknown",
          static_cast<unsigned long long>(frame.diagnostic_request), envelope->mode, result);
  }
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
  if constexpr (kDeviceProfile.has_color_cfa) {
    if (white_submitted &&
        !impl_->WaitForMarkerStart(before_target_marker, frame.sequence, "target",
                                   target_call_returned)) {
      return false;
    }
    // Only accepted targets define history. Never use conversion-stage RGB
    // history: that stage can process candidates which are later discarded.
    if (quality_enabled && frame.sleep_image_epoch == 0)
      impl_->quality_history.Accept(impl_->color_input);
    else
      impl_->quality_history.Reset();
    if (white_submitted) {
      ++impl_->diagnostics.quality_sequences;
      impl_->diagnostics.quality_whitened_pixels = whitened_pixels;
      impl_->diagnostics.latest_quality_ns = ToNanoseconds(
              std::chrono::steady_clock::now() - quality_started);
    }
  }
  if (automatic_quality_pair && white_submitted) {
    // Hold the serial worker only for this pair; ordinary frames retain the
    // existing rate policy. The normal interval also applies after this hold.
    std::this_thread::sleep_until(target_call_returned + quality_after);
    ALOGI("quality_timing sequence=%llu configured_ms=%lld/%lld/%lld "
          "before_ns=%llu between_ns=%llu after_ns=%llu completion=unknown",
          static_cast<unsigned long long>(frame.sequence),
          static_cast<long long>(quality_before.count()),
          static_cast<long long>(quality_between.count()),
          static_cast<long long>(quality_after.count()),
          static_cast<unsigned long long>(ToNanoseconds(white_call_started - previous_submission)),
          static_cast<unsigned long long>(ToNanoseconds(target_call_started - white_call_returned)),
          static_cast<unsigned long long>(ToNanoseconds(std::chrono::steady_clock::now() - target_call_returned)));
  }
  impl_->last_submission = std::chrono::steady_clock::now();
  impl_->completion_observer.RecordAccepted(
          frame.sequence, ToNanoseconds(impl_->last_submission.time_since_epoch()));
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
