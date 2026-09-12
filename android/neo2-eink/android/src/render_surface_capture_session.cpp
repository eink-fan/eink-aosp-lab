#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "Neo2EinkCapture"

#include "neo2/eink/android_eink_pipeline.h"
#include "neo2/eink/android_grayscale_converter.h"
#include "neo2/eink/gpu_snapshot_pool.h"
#include "neo2/eink/render_surface_capture_session.h"
#include "neo2/eink/sleep_image_binder_service.h"

#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
#include "neo2/eink/frontlight_direct_backend.h"
#include "neo2/eink/lower_engine_direct_backend.h"
#else
#include <eink_capture_only_engine.h>
#endif
#include <eink_presentation_adapter.h>
#include <eink_presentation_arm_gate.h>
#include <eink_capture_admission_policy.h>
#include <eink_capture_pressure_accounting.h>
#include <eink_debug_property.h>
#include <eink_full_pool_resample_policy.h>

#include <android-base/properties.h>

#include <log/log.h>

#include <chrono>
#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <unistd.h>

namespace neo2::eink::android {

namespace {

constexpr auto kM3DiagnosticsInterval = std::chrono::seconds(5);
constexpr std::size_t kM3SnapshotSlots = 2;
constexpr char kLateSampleWindowProperty[] = "debug.neo2.eink.late_sample_window_ms";
constexpr char kR1SourceProperty[] = "debug.neo2.eink.r1_source";
constexpr char kR1SourceAuditProperty[] = "debug.neo2.eink.r1_source_audit";
constexpr char kSleepCycleTraceProperty[] = "debug.neo2.eink.sleep_trace";
constexpr auto kR1SourceFenceWait = std::chrono::milliseconds(25);

enum class R1SourceMode : std::uint8_t {
  kSnapshot,
  kDirect,
};

std::mutex g_snapshot_mutex;
std::string g_snapshot = "neo2_eink_diagnostics unavailable\n";
std::mutex g_fresh_snapshot_session_mutex;
RenderSurfaceCaptureSession* g_fresh_snapshot_session = nullptr;

void PublishSnapshot(const std::string& snapshot) {
  std::lock_guard lock(g_snapshot_mutex);
  g_snapshot = snapshot;
}

class FullPoolResampleBridge final {
 public:
  struct Diagnostics {
    std::uint64_t release_notifications = 0;
    std::uint64_t request_failures = 0;
  };

  explicit FullPoolResampleBridge(RenderSurfaceCaptureSession::CompositeRequest request_composite)
      : request_composite_(std::move(request_composite)) {}

  [[nodiscard]] bool CanRequest() const { return static_cast<bool>(request_composite_); }

  [[nodiscard]] neo2::eink::FullPoolObservationDecision ObserveFullPool(
          bool pool_full, const neo2::eink::FullPoolPipelineState& state,
          std::chrono::steady_clock::time_point now) {
    std::lock_guard lock(mutex_);
    return policy_.ObserveFullPool(pool_full, state, now);
  }

  void OnResourceReleased() {
    neo2::eink::FullPoolReleaseDecision release =
            neo2::eink::FullPoolReleaseDecision::kNone;
    {
      std::lock_guard lock(mutex_);
      ++diagnostics_.release_notifications;
      release = policy_.OnResourceReleased(std::chrono::steady_clock::now());
    }
    if (release == neo2::eink::FullPoolReleaseDecision::kNone) return;
    if (request_composite_ && request_composite_()) return;
    std::lock_guard lock(mutex_);
    ++diagnostics_.request_failures;
    policy_.Cancel();
  }

  [[nodiscard]] neo2::eink::FullPoolCaptureDecision TakeCaptureOpportunity() {
    std::lock_guard lock(mutex_);
    return policy_.TakeCaptureOpportunity();
  }

  void RecordCaptureOutcome(neo2::eink::FullPoolCaptureDecision decision, bool copied) {
    std::lock_guard lock(mutex_);
    policy_.RecordCaptureOutcome(decision, copied);
  }

  void Cancel() {
    std::lock_guard lock(mutex_);
    policy_.Cancel();
  }

  [[nodiscard]] neo2::eink::FullPoolResamplePolicy::Diagnostics policy_diagnostics() const {
    std::lock_guard lock(mutex_);
    return policy_.diagnostics();
  }

  [[nodiscard]] Diagnostics diagnostics() const {
    std::lock_guard lock(mutex_);
    return diagnostics_;
  }

 private:
  mutable std::mutex mutex_;
  neo2::eink::FullPoolResamplePolicy policy_;
  RenderSurfaceCaptureSession::CompositeRequest request_composite_;
  Diagnostics diagnostics_;
};

class SnapshotLease final {
 public:
  SnapshotLease(GpuSnapshotPool& pool, GpuSnapshotPool::Snapshot snapshot,
                std::shared_ptr<FullPoolResampleBridge> resample_bridge)
      : pool_(pool), snapshot_(std::move(snapshot)), resample_bridge_(std::move(resample_bridge)) {}

  ~SnapshotLease() {
    if (pool_.ReleaseAfterConversion(snapshot_) && resample_bridge_) {
      resample_bridge_->OnResourceReleased();
    }
  }

 private:
  GpuSnapshotPool& pool_;
  GpuSnapshotPool::Snapshot snapshot_;
  std::shared_ptr<FullPoolResampleBridge> resample_bridge_;
};

struct FirstCaptureOnlySubmissionLog final {
  std::atomic_bool logged = false;
};

}  // namespace

std::string GetNeo2EinkDiagnosticsSnapshot() {
  std::lock_guard lock(g_snapshot_mutex);
  return g_snapshot;
}

std::string GetNeo2EinkFreshDiagnosticsSnapshot() {
  // This process-private entry point is for a privileged debug collector. A
  // fresh observation remains disabled unless trace diagnostics are explicitly
  // enabled, and it never changes the cached periodic line or pipeline state.
  if (!::android::base::GetBoolProperty(kSleepCycleTraceProperty, false)) {
    return "neo2_eink_diagnostics_fresh disabled\n";
  }
  std::lock_guard lock(g_fresh_snapshot_session_mutex);
  return g_fresh_snapshot_session ? g_fresh_snapshot_session->FreshDiagnosticsSnapshot()
                                  : "neo2_eink_diagnostics_fresh unavailable\n";
}

class RenderSurfaceCaptureSession::M3State final {
 public:
  struct SleepImageCatalogRequest {
    std::uint64_t epoch = 0;
    int width = 0;
    int height = 0;
    std::vector<EinkSleepImageCatalog::Entry> entries;
    std::size_t selected_index = 0;
    SleepImagePresentationMode mode = SleepImagePresentationMode::kOpaque;
  };

#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
  // This branch exists only in the separately reviewed M4 output graft. The
  // direct backend's InitializeForOneShot() is called before the first owned
  // snapshot, requires the exact policy/resources/property contract, and
  // uses the selected profile default, with a finite diagnostic override
  // and pacing independent of the lifetime submission budget.
  neo2::eink::android::LowerEngineDirectBackend engine;
  neo2::eink::android::FrontLightDirectBackend front_light;
#else
  M3State()
      : first_submission_log(std::make_shared<FirstCaptureOnlySubmissionLog>()),
        engine([state = first_submission_log](const neo2::eink::CaptureOnlySubmission& submission) {
          if (!state->logged.exchange(true)) {
            ALOGI("M3 first CaptureOnlyEngine submission: sequence=%llu rect=%d,%d-%d,%d "
                  "mode=%d policy=%d grayscale=%d",
                  static_cast<unsigned long long>(submission.sequence), submission.dirty_rect.left,
                  submission.dirty_rect.top, submission.dirty_rect.right, submission.dirty_rect.bottom,
                  submission.engine_mode, submission.policy_flag, submission.has_grayscale_staging);
          }
        }) {}

  std::shared_ptr<FirstCaptureOnlySubmissionLog> first_submission_log;
  neo2::eink::CaptureOnlyEngine engine;
#endif
  std::unique_ptr<GpuSnapshotPool> snapshot_pool;
  std::shared_ptr<AndroidEinkPipeline> pipeline;
  std::shared_ptr<FullPoolResampleBridge> resample_bridge;
  neo2::eink::CaptureAdmissionPolicy capture_admission;
  neo2::eink::CapturePressureAccounting pressure_accounting;
  std::chrono::steady_clock::time_point last_diagnostics{};
  std::uint64_t copy_attempted = 0;
  std::uint64_t copy_submitted = 0;
  std::uint64_t copy_skipped_retained = 0;
  std::uint64_t late_sample_taken = 0;
  std::uint64_t late_sample_missed = 0;
  std::uint64_t late_sample_config_rejections = 0;
  std::string last_invalid_late_sample_window;
  std::uint64_t disarmed = 0;
  AndroidGrayscaleConverter direct_converter;
  R1SourceMode r1_source_mode = R1SourceMode::kSnapshot;
  bool r1_source_audit = false;
  bool r1_source_configured = false;
  std::uint64_t r1_source_mode_transitions = 0;
  std::uint64_t r1_source_config_rejections = 0;
  std::string last_invalid_r1_source;
  std::uint64_t r1_source_copy_attempted = 0;
  std::uint64_t r1_source_copy_completed = 0;
  std::uint64_t r1_source_fence_not_ready = 0;
  std::uint64_t r1_source_copy_failed = 0;
  std::uint64_t r1_source_direct_no_output = 0;
#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
  neo2::eink::PresentationArmGate presentation_arm_gate;
#endif
  bool presentation_enabled = true;
  bool setup_failed = false;
  std::mutex sleep_image_mutex;
  std::optional<SleepImageCatalogRequest> pending_sleep_image_catalog;
  std::optional<std::uint64_t> pending_sleep_cycle_arm;
  std::uint64_t sleep_image_catalog_epoch = 0;
  std::uint64_t sleep_image_last_arm_epoch = 0;
  std::uint64_t sleep_image_catalog_accepted = 0;
  std::uint64_t sleep_image_catalog_rejected = 0;
  std::uint64_t sleep_image_candidate_selections = 0;
  std::uint64_t sleep_cycle_arm_dispatches = 0;
  std::uint64_t sleep_cycle_arm_accepted = 0;
  std::uint64_t sleep_cycle_arm_rejected = 0;
  std::uint64_t sleep_cycle_disarm_dispatches = 0;
  std::uint64_t sleep_cycle_disarm_accepted = 0;
  std::uint64_t sleep_cycle_disarm_rejected = 0;
  std::atomic_bool sleep_image_diagnostics_dirty = false;
};

RenderSurfaceCaptureSession::RenderSurfaceCaptureSession() : capture_(sink_) {}

RenderSurfaceCaptureSession::RenderSurfaceCaptureSession(
        ::android::renderengine::RenderEngine& render_engine, std::uint32_t width,
        std::uint32_t height, CompositeRequest request_composite)
    : capture_(sink_), render_engine_(&render_engine), expected_width_(width), expected_height_(height),
      request_composite_(std::move(request_composite)), m3_(std::make_unique<M3State>()) {
  std::lock_guard lock(g_fresh_snapshot_session_mutex);
  g_fresh_snapshot_session = this;
  RegisterSleepImageCaptureSession(this);
}

RenderSurfaceCaptureSession::~RenderSurfaceCaptureSession() {
  {
    std::lock_guard lock(g_fresh_snapshot_session_mutex);
    if (g_fresh_snapshot_session == this) g_fresh_snapshot_session = nullptr;
  }
  UnregisterSleepImageCaptureSession(this);
  Stop();
}

void RenderSurfaceCaptureSession::StartMetadataOnly() {
  if (started_) {
    return;
  }
  started_ = true;
}

void RenderSurfaceCaptureSession::Stop() {
  if (m3_ && m3_->resample_bridge) m3_->resample_bridge->Cancel();
  if (m3_) {
    std::shared_ptr<AndroidEinkPipeline> pipeline;
    {
      std::lock_guard lock(m3_->sleep_image_mutex);
      if (!started_) return;
      pipeline = std::move(m3_->pipeline);
      m3_->snapshot_pool.reset();
    }
    if (pipeline) pipeline->Stop();
  }
  started_ = false;
}

EinkSleepImageCatalog::PublishResult RenderSurfaceCaptureSession::PublishSleepImageCatalog(
        std::uint64_t epoch, int width, int height,
        std::vector<EinkSleepImageCatalog::Entry> entries, std::size_t selected_index,
        SleepImagePresentationMode mode) {
  if (!m3_ || epoch == 0) return EinkSleepImageCatalog::PublishResult::kRejectedSelection;
  if (width != EinkSleepImageCatalog::kPanelWidth ||
      height != EinkSleepImageCatalog::kPanelHeight) {
    return EinkSleepImageCatalog::PublishResult::kRejectedGeometry;
  }
  if (mode != SleepImagePresentationMode::kOpaque &&
      mode != SleepImagePresentationMode::kOverlay) {
    return EinkSleepImageCatalog::PublishResult::kRejectedSelection;
  }
  EinkSleepImageCatalog validation_catalog(width, height);
  const auto validation = validation_catalog.Publish(entries);
  if (validation != EinkSleepImageCatalog::PublishResult::kAccepted) return validation;
  if (selected_index >= validation_catalog.size()) {
    return EinkSleepImageCatalog::PublishResult::kRejectedSelection;
  }
  std::lock_guard lock(m3_->sleep_image_mutex);
  // The framework only publishes validated provider callbacks while awake.
  // Reject stale generations so an older Binder result cannot replace the
  // catalog retained for the next terminal-black data-plane cycle.
  if (epoch <= m3_->sleep_image_catalog_epoch) {
    ++m3_->sleep_image_catalog_rejected;
    m3_->sleep_image_diagnostics_dirty.store(true);
    return EinkSleepImageCatalog::PublishResult::kRejectedSelection;
  }
  if (m3_->pipeline) {
    const auto result = m3_->pipeline->PublishSleepImageCatalog(
            epoch, width, height, std::move(entries), selected_index, mode);
    if (result == EinkSleepImageCatalog::PublishResult::kAccepted) {
      m3_->sleep_image_catalog_epoch = epoch;
      ++m3_->sleep_image_catalog_accepted;
      ++m3_->sleep_image_candidate_selections;
    } else {
      ++m3_->sleep_image_catalog_rejected;
    }
    m3_->sleep_image_diagnostics_dirty.store(true);
    return result;
  }
  m3_->pending_sleep_image_catalog = M3State::SleepImageCatalogRequest{
          .epoch = epoch,
          .width = width,
          .height = height,
          .entries = std::move(entries),
          .selected_index = selected_index,
          .mode = mode,
  };
  m3_->sleep_image_catalog_epoch = epoch;
  ++m3_->sleep_image_catalog_accepted;
  ++m3_->sleep_image_candidate_selections;
  m3_->sleep_image_diagnostics_dirty.store(true);
  return EinkSleepImageCatalog::PublishResult::kAccepted;
}

SleepImageArmResult RenderSurfaceCaptureSession::ArmSleepCycle(std::uint64_t epoch) {
  if (!m3_ || epoch == 0) return SleepImageArmResult::kRejectedZero;
  std::lock_guard lock(m3_->sleep_image_mutex);
  ++m3_->sleep_cycle_arm_dispatches;
  SleepImageArmResult result = SleepImageArmResult::kRejectedMismatched;
  if (epoch == m3_->sleep_image_catalog_epoch) {
    if (m3_->pipeline) {
      result = m3_->pipeline->ArmSleepCycle(epoch);
    } else if (epoch < m3_->sleep_image_last_arm_epoch) {
      result = SleepImageArmResult::kRejectedStale;
    } else if (epoch == m3_->sleep_image_last_arm_epoch) {
      result = SleepImageArmResult::kRejectedDuplicate;
    } else {
      m3_->pending_sleep_cycle_arm = epoch;
      result = SleepImageArmResult::kAccepted;
    }
  }
  if (result == SleepImageArmResult::kAccepted) {
    m3_->sleep_image_last_arm_epoch = epoch;
    ++m3_->sleep_cycle_arm_accepted;
  } else {
    ++m3_->sleep_cycle_arm_rejected;
  }
  m3_->sleep_image_diagnostics_dirty.store(true);
  if (::android::base::GetBoolProperty(kSleepCycleTraceProperty, false)) {
    ALOGI("sleep_cycle_arm_receipt epoch=%llu result=%d",
          static_cast<unsigned long long>(epoch), static_cast<int>(result));
  }
  return result;
}

SleepImagePresentationResult RenderSurfaceCaptureSession::PresentSleepImage(
        std::uint64_t epoch) {
  if (!m3_ || epoch == 0) return SleepImagePresentationResult::kRejectedZero;
  std::lock_guard lock(m3_->sleep_image_mutex);
  if (epoch != m3_->sleep_image_catalog_epoch) {
    return SleepImagePresentationResult::kRejectedCatalog;
  }
  if (!m3_->pipeline) return SleepImagePresentationResult::kRejectedUnavailable;
  return m3_->pipeline->PresentSleepImage(epoch);
}

SleepImageDisarmResult RenderSurfaceCaptureSession::DisarmSleepCycle(std::uint64_t epoch) {
  if (!m3_ || epoch == 0) return SleepImageDisarmResult::kRejectedZero;
  std::lock_guard lock(m3_->sleep_image_mutex);
  ++m3_->sleep_cycle_disarm_dispatches;
  SleepImageDisarmResult result = SleepImageDisarmResult::kRejectedInactive;
  if (m3_->pipeline) {
    result = m3_->pipeline->DisarmSleepCycle(epoch);
  } else if (m3_->pending_sleep_cycle_arm && *m3_->pending_sleep_cycle_arm == epoch) {
    m3_->pending_sleep_cycle_arm.reset();
    result = SleepImageDisarmResult::kAccepted;
  } else if (m3_->pending_sleep_cycle_arm) {
    result = SleepImageDisarmResult::kRejectedMismatched;
  }
  if (result == SleepImageDisarmResult::kAccepted) {
    ++m3_->sleep_cycle_disarm_accepted;
  } else {
    ++m3_->sleep_cycle_disarm_rejected;
  }
  m3_->sleep_image_diagnostics_dirty.store(true);
  if (::android::base::GetBoolProperty(kSleepCycleTraceProperty, false)) {
    ALOGI("sleep_cycle_disarm_receipt epoch=%llu result=%d",
          static_cast<unsigned long long>(epoch), static_cast<int>(result));
  }
  return result;
}

std::string RenderSurfaceCaptureSession::FreshDiagnosticsSnapshot() const {
  if (!m3_) return "neo2_eink_diagnostics_fresh unavailable\n";
  std::shared_ptr<AndroidEinkPipeline> pipeline;
  std::uint64_t catalog_epoch = 0;
  std::uint64_t arm_dispatches = 0;
  std::uint64_t arm_accepted = 0;
  std::uint64_t arm_rejected = 0;
  std::uint64_t disarm_dispatches = 0;
  std::uint64_t disarm_accepted = 0;
  std::uint64_t disarm_rejected = 0;
  std::uint64_t lower_submission = 0;
  int lower_result = 0;
  {
    std::lock_guard lock(m3_->sleep_image_mutex);
    pipeline = m3_->pipeline;
    catalog_epoch = m3_->sleep_image_catalog_epoch;
    arm_dispatches = m3_->sleep_cycle_arm_dispatches;
    arm_accepted = m3_->sleep_cycle_arm_accepted;
    arm_rejected = m3_->sleep_cycle_arm_rejected;
    disarm_dispatches = m3_->sleep_cycle_disarm_dispatches;
    disarm_accepted = m3_->sleep_cycle_disarm_accepted;
    disarm_rejected = m3_->sleep_cycle_disarm_rejected;
#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
    const auto lower_diagnostics = m3_->engine.diagnostics();
    lower_submission = lower_diagnostics.pending_submission_id;
    lower_result = lower_diagnostics.update_result;
#endif
  }
  const auto diagnostics = pipeline ? pipeline->diagnostics() : AndroidEinkPipeline::Diagnostics{};
  char snapshot[512];
  const int count = std::snprintf(snapshot, sizeof(snapshot),
          "neo2_eink_diagnostics_fresh catalog_epoch=%llu arm=%llu/%llu/%llu "
          "disarm=%llu/%llu/%llu active_epoch=%llu latch_before=%u decision=%u "
          "conversion_sequence=%llu enqueue=%u mode=%u overlay=%llu/%llu/%llu/%llu/%llu/%llu "
          "lower_submission=%llu lower_result=%d\n",
          static_cast<unsigned long long>(catalog_epoch),
          static_cast<unsigned long long>(arm_dispatches),
          static_cast<unsigned long long>(arm_accepted),
          static_cast<unsigned long long>(arm_rejected),
          static_cast<unsigned long long>(disarm_dispatches),
          static_cast<unsigned long long>(disarm_accepted),
          static_cast<unsigned long long>(disarm_rejected),
          static_cast<unsigned long long>(diagnostics.sleep_cycle_active_epoch),
          diagnostics.sleep_cycle_latch_state_before_decision,
          diagnostics.sleep_cycle_last_decision,
          static_cast<unsigned long long>(diagnostics.sleep_cycle_last_conversion_sequence),
          diagnostics.sleep_cycle_last_enqueue_result,
          diagnostics.sleep_image_mode,
          static_cast<unsigned long long>(diagnostics.sleep_overlay_background_retained),
          static_cast<unsigned long long>(diagnostics.sleep_overlay_background_rejected),
          static_cast<unsigned long long>(diagnostics.sleep_overlay_composited),
          static_cast<unsigned long long>(diagnostics.sleep_overlay_no_background),
          static_cast<unsigned long long>(diagnostics.sleep_overlay_invalid),
          static_cast<unsigned long long>(diagnostics.sleep_overlay_background_released),
          static_cast<unsigned long long>(lower_submission), lower_result);
  return count > 0 && static_cast<std::size_t>(count) < sizeof(snapshot)
          ? std::string(snapshot, static_cast<std::size_t>(count))
          : "neo2_eink_diagnostics_fresh unavailable\n";
}

void RenderSurfaceCaptureSession::Capture(
        const std::shared_ptr<::android::renderengine::ExternalTexture>& texture,
        const ::android::base::unique_fd& ready_fence) {
  if (!started_) {
    return;
  }
  capture_.Capture(texture, ready_fence);

#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
  // Manual controls are demand driven by the process-lifetime Binder service.
  // Capture cadence must not poll or write the front light.
#endif

  const auto metadata = sink_.LatestCapture();
  if (!metadata) {
    return;
  }
  if (metadata->geometry_status != reported_geometry_status_) {
    reported_geometry_status_ = metadata->geometry_status;
    if (reported_geometry_status_ == PanelGeometryStatus::kVerified) {
      ALOGI("metadata capture geometry verified: %ux%u format=%d buffer=%llu",
            metadata->width, metadata->height, metadata->pixel_format,
            static_cast<unsigned long long>(metadata->buffer_id));
    } else if (reported_geometry_status_ == PanelGeometryStatus::kRejected) {
      ALOGE("metadata capture geometry rejected: got %ux%u, expected 1448x1072; "
            "pixel work remains disabled",
            metadata->width, metadata->height);
    }
  }

  MaybeStartM3();
  CaptureM3(texture, ready_fence);
}

void RenderSurfaceCaptureSession::MaybeStartM3() {
  if (!m3_ || m3_->setup_failed || !PixelWorkPermitted()) {
    return;
  }
  {
    std::lock_guard lock(m3_->sleep_image_mutex);
    if (m3_->pipeline) return;
  }
  if (render_engine_ == nullptr || expected_width_ != 1448 || expected_height_ != 1072) {
    ALOGE("M3 snapshot disabled: RenderSurface dimensions are %ux%u, expected 1448x1072",
          expected_width_, expected_height_);
    m3_->setup_failed = true;
    return;
  }
#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
  // R2 boot isolation: metadata collection remains available while the
  // existing volatile enable is false, but no private engine object is
  // initialized or touched. A later explicit enable on a booted guest causes
  // the next normal RenderSurface capture to enter the one-shot setup below.
  if (m3_->presentation_arm_gate.Observe(LowerEngineDirectBackend::PresentationEnabled()) ==
      neo2::eink::PresentationArmGate::Decision::kDisarmed) {
    return;
  }
  // This call is output-sensitive and can only be reached when the explicit
  // M4 product/build graft defines NEO2_EINK_M4_LOWER_ENGINE_OUTPUT. A failed
  // preflight or initializer is terminal for this process and leaves M3 pixel
  // capture disabled; no fallback engine is selected.
  if (!m3_->engine.InitializeForOneShot()) {
    const auto diagnostics = m3_->engine.diagnostics();
    ALOGE("M4 lower-engine one-shot setup rejected: state=%d preflight=0x%x init=%d",
          static_cast<int>(diagnostics.state), diagnostics.preflight_failures,
          diagnostics.initializer_result);
    m3_->setup_failed = true;
    return;
  }
#endif

  auto snapshot_pool =
          GpuSnapshotPool::Create(*render_engine_, expected_width_, expected_height_, kM3SnapshotSlots);
  if (!snapshot_pool) {
    ALOGE("M3 snapshot disabled: unable to allocate %zu owned GPU destinations", kM3SnapshotSlots);
    m3_->setup_failed = true;
    return;
  }
  m3_->snapshot_pool = std::move(snapshot_pool);
  m3_->resample_bridge = std::make_shared<FullPoolResampleBridge>(request_composite_);
  auto pipeline = std::make_shared<AndroidEinkPipeline>(
          m3_->engine, neo2::eink::Rotation::kNone,
#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
          // R5 sends every changed composition through the observed normal
          // full-input differential record; it does not infer a damage rect.
          neo2::eink::ToEngineMode(neo2::eink::RefreshMode::kGlr16Partial),
#else
          neo2::eink::ToEngineMode(neo2::eink::RefreshMode::kAutomatic),
#endif
          0,
#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
          [] { return LowerEngineDirectBackend::MinimumSubmissionInterval(); }
#else
          {}
#endif
  );
  std::optional<M3State::SleepImageCatalogRequest> pending_catalog;
  std::optional<std::uint64_t> pending_arm;
  {
    std::lock_guard lock(m3_->sleep_image_mutex);
    if (m3_->pipeline) return;
    m3_->pipeline = pipeline;
    if (m3_->pending_sleep_image_catalog) {
      pending_catalog = std::move(*m3_->pending_sleep_image_catalog);
      m3_->pending_sleep_image_catalog.reset();
    }
    pending_arm = m3_->pending_sleep_cycle_arm;
    m3_->pending_sleep_cycle_arm.reset();
  }
  if (pending_catalog) {
    const auto publish = pipeline->PublishSleepImageCatalog(
            pending_catalog->epoch, pending_catalog->width, pending_catalog->height,
            std::move(pending_catalog->entries), pending_catalog->selected_index,
            pending_catalog->mode);
    if (publish != EinkSleepImageCatalog::PublishResult::kAccepted) {
      ALOGW("pending sleep-image catalog rejected before first capture: result=%d",
            static_cast<int>(publish));
    }
  }
  if (pending_arm) {
    const auto arm = pipeline->ArmSleepCycle(*pending_arm);
    if (arm != SleepImageArmResult::kAccepted) {
      ALOGW("sleep_cycle_pending_arm rejected: epoch=%llu result=%d",
            static_cast<unsigned long long>(*pending_arm), static_cast<int>(arm));
    }
  }
  pipeline->Start();
  pipeline->SetEnabled(true);
  m3_->last_diagnostics = std::chrono::steady_clock::now();
#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
  ALOGI("M4 lower-engine refresh controller armed: %zu owned RGBA destinations at 1448x1072 "
        "continuous=%d active_limit=%llu interval_ms=%lld",
        m3_->snapshot_pool->capacity(),
        LowerEngineDirectBackend::ContinuousModeEnabled(),
        static_cast<unsigned long long>(LowerEngineDirectBackend::ActiveSubmissionLimit()),
        static_cast<long long>(LowerEngineDirectBackend::MinimumSubmissionInterval().count()));
#else
  ALOGI("M3 capture-only snapshot enabled: %zu owned RGBA destinations at 1448x1072",
        m3_->snapshot_pool->capacity());
#endif
}

void RenderSurfaceCaptureSession::CaptureM3(
        const std::shared_ptr<::android::renderengine::ExternalTexture>& texture,
        const ::android::base::unique_fd& ready_fence) {
  if (!m3_ || !m3_->snapshot_pool) {
    return;
  }
  std::shared_ptr<AndroidEinkPipeline> pipeline;
  {
    std::lock_guard lock(m3_->sleep_image_mutex);
    pipeline = m3_->pipeline;
  }
  if (!pipeline) return;
#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
  if (!LowerEngineDirectBackend::PresentationEnabled()) {
    if (m3_->presentation_enabled) {
      m3_->presentation_enabled = false;
      pipeline->SetEnabled(false);
      ALOGW("M4 lower-engine presentation disarmed by debug.neo2.eink.enabled=0");
    }
    ++m3_->disarmed;
    MaybeLogM3Diagnostics();
    return;
  }
  if (!m3_->presentation_enabled) {
    m3_->presentation_enabled = true;
    pipeline->SetEnabled(true);
    ALOGI("M4 lower-engine presentation rearmed by debug.neo2.eink.enabled=1");
  }
  // This remains event-driven: only naturally rendered SurfaceFlinger frames
  // are captured. The active finite budget applies to panel submissions, not
  // capture opportunities, so a rate-limited app transition can leave a
  // newest frame for the adapter's trailing submit.
  // It is not a general Android presenter and does not infer physical
  // completion.
  if (m3_->engine.diagnostics().submissions >= LowerEngineDirectBackend::ActiveSubmissionLimit()) {
    return;
  }
#endif
  const std::string raw_r1_source = ::android::base::GetProperty(kR1SourceProperty, "");
  R1SourceMode requested_r1_source = R1SourceMode::kSnapshot;
  bool r1_source_invalid = false;
  if (raw_r1_source == "direct") {
    requested_r1_source = R1SourceMode::kDirect;
  } else if (!raw_r1_source.empty() && raw_r1_source != "snapshot") {
    r1_source_invalid = true;
  }
  if (r1_source_invalid && m3_->last_invalid_r1_source != raw_r1_source) {
    ++m3_->r1_source_config_rejections;
    m3_->last_invalid_r1_source = raw_r1_source;
    ALOGW("R1 source selector rejected; using snapshot");
  } else if (!r1_source_invalid) {
    m3_->last_invalid_r1_source.clear();
  }
  const bool requested_r1_audit = requested_r1_source == R1SourceMode::kDirect ||
          ::android::base::GetBoolProperty(kR1SourceAuditProperty, false);
  if (m3_->r1_source_configured && requested_r1_source != m3_->r1_source_mode) {
    m3_->r1_source_mode = requested_r1_source;
    m3_->r1_source_audit = requested_r1_audit;
    ++m3_->r1_source_mode_transitions;
    pipeline->ResetForSourceChange();
    // Discard the transition capture so no previous source baseline can be
    // compared or submitted after a live property switch.
    MaybeLogM3Diagnostics();
    return;
  }
  m3_->r1_source_mode = requested_r1_source;
  m3_->r1_source_audit = requested_r1_audit;
  m3_->r1_source_configured = true;
  const auto now = std::chrono::steady_clock::now();
  const std::string raw_late_sample_window =
          ::android::base::GetProperty(kLateSampleWindowProperty, "");
  const auto late_sample_window =
          neo2::eink::ParseBoundedDebugInt(raw_late_sample_window, 100, 25, 250);
  if (late_sample_window.invalid &&
      m3_->last_invalid_late_sample_window != raw_late_sample_window) {
    ++m3_->late_sample_config_rejections;
    m3_->last_invalid_late_sample_window = raw_late_sample_window;
    ALOGW("M3 late-sample window rejected; using the B001 default");
  } else if (!late_sample_window.invalid) {
    m3_->last_invalid_late_sample_window.clear();
  }
  static_cast<void>(m3_->capture_admission.Reconfigure(
          {.late_sample_window = std::chrono::milliseconds(late_sample_window.value)}));
  const auto admission_state = pipeline->capture_admission_state();
  const auto resample_capture = m3_->resample_bridge
          ? m3_->resample_bridge->TakeCaptureOpportunity()
          : neo2::eink::FullPoolCaptureDecision::kNormalCapture;
  const bool is_resample_capture =
          resample_capture != neo2::eink::FullPoolCaptureDecision::kNormalCapture;
  bool late_replacement = false;
  if (!is_resample_capture) {
    const auto admission = m3_->capture_admission.Evaluate(
            admission_state.retained_candidate, admission_state.next_eligible, now);
    m3_->pressure_accounting.RecordOpportunity(m3_->capture_admission.last_reason());
    if (admission == neo2::eink::CaptureAdmissionDecision::kSkipRetainedCandidate) {
      ++m3_->copy_skipped_retained;
      MaybeLogM3Diagnostics();
      return;
    }
    late_replacement = admission == neo2::eink::CaptureAdmissionDecision::kCaptureLateReplacement;
    if (late_replacement) ++m3_->late_sample_taken;
  }
  const auto pool_busy_before = m3_->snapshot_pool->busy_count();
  const auto pool_capacity = m3_->snapshot_pool->capacity();
  const neo2::eink::FullPoolPipelineState pipeline_state{
          .conversion_in_flight = admission_state.conversion_in_flight,
          .pending_capture = admission_state.pending_capture,
          .adapter_retained = admission_state.adapter_retained,
  };
  // The resample policy and pressure accounting deliberately own separate
  // value types. Keep the seam explicit so either contract can evolve
  // without silently coupling its accounting semantics to scheduling.
  const neo2::eink::CapturePressureAccounting::PipelineState pressure_state{
          .conversion_in_flight = pipeline_state.conversion_in_flight,
          .pending_capture = pipeline_state.pending_capture,
          .adapter_retained = pipeline_state.adapter_retained,
  };
  if (!is_resample_capture && m3_->resample_bridge &&
      m3_->resample_bridge->CanRequest()) {
    const auto resample_observation = m3_->resample_bridge->ObserveFullPool(
            pool_capacity != 0 && pool_busy_before >= pool_capacity, pipeline_state, now);
    if (resample_observation != neo2::eink::FullPoolObservationDecision::kIgnored) {
      m3_->pressure_accounting.RecordFullPoolDeferred(pressure_state);
      MaybeLogM3Diagnostics();
      return;
    }
  }
  if (!is_resample_capture) {
    m3_->pressure_accounting.RecordAttempt(pool_busy_before, pool_capacity, pressure_state);
  } else {
    // This fulfillment has no normal admission opportunity: it was the one
    // later queueBuffer callback authorized by a deferred full-pool state.
    // Keep its actual TryCopy outcome in the accounting total separately.
    m3_->pressure_accounting.RecordResampleAttempt();
  }
  // This is deliberately after normal capture admission and full-pool
  // evaluation but still inside RenderSurface's queueBuffer seam. The direct
  // copy consumes the supplied fence and returns only owned grayscale bytes;
  // it never hands the composed source GraphicBuffer to a worker.
  std::shared_ptr<OwnedGrayscaleBuffer> direct_grayscale;
  if (m3_->r1_source_audit) {
    ::android::sp<::android::Fence> source_fence = ::android::Fence::NO_FENCE;
    if (ready_fence.get() >= 0) {
      source_fence = ::android::sp<::android::Fence>::make(dup(ready_fence.get()));
    }
    ++m3_->r1_source_copy_attempted;
    const auto source_copy = m3_->direct_converter.ConvertBeforeQueue(
            {.compose_buffer = texture->getBuffer(),
             .ready_fence = std::move(source_fence),
             .buffer_id = texture->getId(),
             .captured_at = now},
            neo2::eink::Rotation::kNone, kR1SourceFenceWait);
    direct_grayscale = source_copy.grayscale;
    switch (source_copy.status) {
      case AndroidGrayscaleConverter::BoundedConversionStatus::kSuccess:
        ++m3_->r1_source_copy_completed;
        break;
      case AndroidGrayscaleConverter::BoundedConversionStatus::kFenceNotReady:
        ++m3_->r1_source_fence_not_ready;
        break;
      case AndroidGrayscaleConverter::BoundedConversionStatus::kConversionFailed:
        ++m3_->r1_source_copy_failed;
        break;
    }
    if (m3_->r1_source_mode == R1SourceMode::kDirect && !direct_grayscale) {
      ++m3_->r1_source_direct_no_output;
      MaybeLogM3Diagnostics();
      return;
    }
  }
  const auto pool_drops_before = m3_->snapshot_pool->drop_count();
  const auto copy_failures_before = m3_->snapshot_pool->failed_submission_count();
  ++m3_->copy_attempted;
  auto snapshot = m3_->snapshot_pool->TryCopy(texture, ready_fence);
  if (!snapshot) {
    if (is_resample_capture && m3_->resample_bridge) {
      m3_->resample_bridge->RecordCaptureOutcome(resample_capture, false);
    }
    if (m3_->snapshot_pool->drop_count() != pool_drops_before) {
      m3_->pressure_accounting.RecordPoolDrop();
    } else if (m3_->snapshot_pool->failed_submission_count() != copy_failures_before) {
      m3_->pressure_accounting.RecordCopyFailure();
    }
    if (late_replacement) ++m3_->late_sample_missed;
    MaybeLogM3Diagnostics();
    return;
  }
  if (is_resample_capture && m3_->resample_bridge) {
    m3_->resample_bridge->RecordCaptureOutcome(resample_capture, true);
  }
  auto lease = std::make_shared<SnapshotLease>(
          *m3_->snapshot_pool, *snapshot, m3_->resample_bridge);
  CapturedComposition composition{
          .compose_buffer = std::move(snapshot->buffer),
          .ready_fence = std::move(snapshot->ready_fence),
          .buffer_id = 0,
          .ownership_lease = std::move(lease),
          .direct_grayscale = std::move(direct_grayscale),
          .direct_grayscale_selected = m3_->r1_source_mode == R1SourceMode::kDirect,
          .direct_grayscale_audit_requested = m3_->r1_source_audit,
          .captured_at = now,
  };
  pipeline->OnCapturedComposition(std::move(composition));
  ++m3_->copy_submitted;
  m3_->pressure_accounting.RecordCopied();
  if (m3_->copy_submitted == 1) {
    ALOGI("M3 first owned GPU copy submitted: slot=%zu generation=%llu",
          snapshot->slot, static_cast<unsigned long long>(snapshot->generation));
  }
  MaybeLogM3Diagnostics();
}

void RenderSurfaceCaptureSession::MaybeLogM3Diagnostics() {
  if (!m3_ || !m3_->snapshot_pool) {
    return;
  }
  std::shared_ptr<AndroidEinkPipeline> pipeline;
  std::uint64_t sleep_image_catalog_accepted = 0;
  std::uint64_t sleep_image_catalog_rejected = 0;
  std::uint64_t sleep_image_candidate_selections = 0;
  {
    std::lock_guard lock(m3_->sleep_image_mutex);
    pipeline = m3_->pipeline;
    sleep_image_catalog_accepted = m3_->sleep_image_catalog_accepted;
    sleep_image_catalog_rejected = m3_->sleep_image_catalog_rejected;
    sleep_image_candidate_selections = m3_->sleep_image_candidate_selections;
  }
  if (!pipeline) return;
  const auto now = std::chrono::steady_clock::now();
  if (now - m3_->last_diagnostics < kM3DiagnosticsInterval &&
      !m3_->sleep_image_diagnostics_dirty.exchange(false)) {
    return;
  }
  m3_->sleep_image_diagnostics_dirty.store(false);
  m3_->last_diagnostics = now;
  const auto diagnostics = pipeline->diagnostics();
  const auto adapter_diagnostics = pipeline->adapter_diagnostics();
  const auto pressure_diagnostics = m3_->pressure_accounting.diagnostics();
  const auto resample_diagnostics = m3_->resample_bridge->policy_diagnostics();
  const auto resample_bridge_diagnostics = m3_->resample_bridge->diagnostics();
#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
  m3_->engine.ObserveCompletionTimeout();
  const auto lower_diagnostics = m3_->engine.diagnostics();
  const auto front_light_diagnostics = m3_->front_light.diagnostics();
#endif
  const std::uint64_t average_capture_interval_ns = diagnostics.capture_interval_samples == 0
          ? 0
          : diagnostics.capture_interval_total_ns / diagnostics.capture_interval_samples;
  char snapshot[4'096];
  const int snapshot_size = std::snprintf(
          snapshot, sizeof(snapshot),
          "neo2_eink_diagnostics version=1 copy_attempted=%llu copied=%llu skipped_retained=%llu "
          "late_sample_taken=%llu late_sample_missed=%llu late_sample_config_rejects=%llu "
          "panel_gray=stock16 "
          "r1_source=%s r1_audit=%d r1_transitions=%llu r1_config_rejects=%llu "
          "r1_source_copy=%llu/%llu/%llu/%llu/%llu "
          "r1_audit=%llu/%llu/%llu/%llu/%llu direct_selected=%llu "
          "r1_audit_last=%llu/%d,%d,%d,%d "
          "pool_drops=%llu retained_replaced=%llu retained_deferred=%llu retained_submitted=%llu "
          "submit_accepted=%llu submit_rejected=%llu "
          "normal_differential=%llu/%llu/%llu "
          "sleep_image_catalog=%llu/%llu sleep_image_selection=%llu "
          "terminal_black=%llu/%llu/%llu/%llu/%llu/%llu/%llu "
          "terminal_black_suppressions=%llu "
          "resample=%llu/%llu/%llu/%llu/%llu/%llu/%llu resample_release=%llu "
          "resample_request_failures=%llu "
          "pressure_opportunities=%llu pressure_reasons=%llu/%llu/%llu/%llu/%llu "
          "pressure_attempts=%llu pressure_busy=%llu/%llu/%llu pressure_full=%llu "
          "pressure_full_state=%llu/%llu/%llu pressure_outcomes=%llu/%llu/%llu "
          "pressure_deferred=%llu/%llu pressure_resample_attempts=%llu "
          "pressure_reconciled=%d conversion_aggregate=%llu/%llu/%llu "
          "delta_bucket_counts=%llu/%llu/%llu/%llu/%llu delta_last_ppm=%u "
          "delta_last_box=%d,%d,%d,%d capture_to_submit_ns=%llu"
#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
          " lower_rejected=%llu lower_idle=%llu/%llu/%llu/%llu completion_unknown=%llu "
          "timeout_observations=%llu "
          "light_polls=%llu light_polls_skipped=%llu light_poll_rejects=%llu "
          "light_generations=%llu light_valid=%llu light_capped=%llu light_applies=%llu "
          "light_zero_off=%llu light_write_failures=%llu light_control_rejects=%llu "
          "light_latest_apply_ns=%llu"
#endif
          "\n",
          static_cast<unsigned long long>(m3_->copy_attempted),
          static_cast<unsigned long long>(m3_->copy_submitted),
          static_cast<unsigned long long>(m3_->copy_skipped_retained),
          static_cast<unsigned long long>(m3_->late_sample_taken),
          static_cast<unsigned long long>(m3_->late_sample_missed),
          static_cast<unsigned long long>(m3_->late_sample_config_rejections),
          m3_->r1_source_mode == R1SourceMode::kDirect ? "direct" : "snapshot",
          m3_->r1_source_audit ? 1 : 0,
          static_cast<unsigned long long>(m3_->r1_source_mode_transitions),
          static_cast<unsigned long long>(m3_->r1_source_config_rejections),
          static_cast<unsigned long long>(m3_->r1_source_copy_attempted),
          static_cast<unsigned long long>(m3_->r1_source_copy_completed),
          static_cast<unsigned long long>(m3_->r1_source_fence_not_ready),
          static_cast<unsigned long long>(m3_->r1_source_copy_failed),
          static_cast<unsigned long long>(m3_->r1_source_direct_no_output),
          static_cast<unsigned long long>(diagnostics.source_audit_samples),
          static_cast<unsigned long long>(diagnostics.source_audit_exact_matches),
          static_cast<unsigned long long>(diagnostics.source_audit_mismatches),
          static_cast<unsigned long long>(diagnostics.source_audit_incompatible),
          static_cast<unsigned long long>(diagnostics.source_audit_unavailable),
          static_cast<unsigned long long>(diagnostics.source_direct_selected),
          static_cast<unsigned long long>(diagnostics.latest_source_audit_differing_pixels),
          diagnostics.latest_source_audit_left, diagnostics.latest_source_audit_top,
          diagnostics.latest_source_audit_right, diagnostics.latest_source_audit_bottom,
          static_cast<unsigned long long>(m3_->snapshot_pool->drop_count()),
          static_cast<unsigned long long>(adapter_diagnostics.pending_replaced),
          static_cast<unsigned long long>(adapter_diagnostics.trailing_deferred),
          static_cast<unsigned long long>(adapter_diagnostics.trailing_submitted),
          static_cast<unsigned long long>(diagnostics.submit_acceptances),
          static_cast<unsigned long long>(diagnostics.submit_rejections),
          static_cast<unsigned long long>(diagnostics.normal_differential_selected),
          static_cast<unsigned long long>(diagnostics.normal_differential_accepted),
          static_cast<unsigned long long>(diagnostics.normal_differential_rejected),
          static_cast<unsigned long long>(sleep_image_catalog_accepted),
          static_cast<unsigned long long>(sleep_image_catalog_rejected),
          static_cast<unsigned long long>(sleep_image_candidate_selections),
          static_cast<unsigned long long>(diagnostics.terminal_black_candidates),
          static_cast<unsigned long long>(diagnostics.sleep_image_replacements),
          static_cast<unsigned long long>(diagnostics.terminal_black_last_real_holds),
          static_cast<unsigned long long>(diagnostics.terminal_black_duplicate_suppressions),
          static_cast<unsigned long long>(diagnostics.terminal_black_rearm_events),
          static_cast<unsigned long long>(diagnostics.terminal_black_catalog_ready),
          static_cast<unsigned long long>(diagnostics.terminal_black_catalog_missing),
          static_cast<unsigned long long>(diagnostics.sleep_image_suppressions),
          static_cast<unsigned long long>(resample_diagnostics.created),
          static_cast<unsigned long long>(resample_diagnostics.coalesced),
          static_cast<unsigned long long>(resample_diagnostics.dispatched),
          static_cast<unsigned long long>(resample_diagnostics.expiry_fallback),
          static_cast<unsigned long long>(resample_diagnostics.fulfilled),
          static_cast<unsigned long long>(resample_diagnostics.copy_failures),
          static_cast<unsigned long long>(resample_diagnostics.cancelled),
          static_cast<unsigned long long>(resample_bridge_diagnostics.release_notifications),
          static_cast<unsigned long long>(resample_bridge_diagnostics.request_failures),
          static_cast<unsigned long long>(pressure_diagnostics.opportunities),
          static_cast<unsigned long long>(pressure_diagnostics.no_retained_candidate),
          static_cast<unsigned long long>(pressure_diagnostics.no_next_eligible_slot),
          static_cast<unsigned long long>(pressure_diagnostics.elapsed_output_slot),
          static_cast<unsigned long long>(pressure_diagnostics.closed_slot_skip),
          static_cast<unsigned long long>(pressure_diagnostics.late_replacement),
          static_cast<unsigned long long>(pressure_diagnostics.attempts),
          static_cast<unsigned long long>(pressure_diagnostics.attempts_busy_zero),
          static_cast<unsigned long long>(pressure_diagnostics.attempts_busy_one),
          static_cast<unsigned long long>(pressure_diagnostics.attempts_busy_two_or_more),
          static_cast<unsigned long long>(pressure_diagnostics.attempts_pool_full),
          static_cast<unsigned long long>(pressure_diagnostics.full_pool_conversion_in_flight),
          static_cast<unsigned long long>(pressure_diagnostics.full_pool_pending_capture),
          static_cast<unsigned long long>(pressure_diagnostics.full_pool_adapter_retained),
          static_cast<unsigned long long>(pressure_diagnostics.copied),
          static_cast<unsigned long long>(pressure_diagnostics.pool_drops),
          static_cast<unsigned long long>(pressure_diagnostics.copy_failures),
          static_cast<unsigned long long>(pressure_diagnostics.full_pool_deferred),
          static_cast<unsigned long long>(
                  pressure_diagnostics.full_pool_deferred_conversion_pending),
          static_cast<unsigned long long>(pressure_diagnostics.resample_attempts),
          m3_->pressure_accounting.HasReconciledAccounting() ? 1 : 0,
          static_cast<unsigned long long>(diagnostics.conversion_duration_samples),
          static_cast<unsigned long long>(diagnostics.conversion_duration_total_ns),
          static_cast<unsigned long long>(diagnostics.conversion_duration_max_ns),
          static_cast<unsigned long long>(diagnostics.delta_no_change),
          static_cast<unsigned long long>(diagnostics.delta_under_one_percent),
          static_cast<unsigned long long>(diagnostics.delta_under_five_percent),
          static_cast<unsigned long long>(diagnostics.delta_under_twenty_five_percent),
          static_cast<unsigned long long>(diagnostics.delta_twenty_five_percent_or_more),
          diagnostics.latest_delta_coverage_ppm, diagnostics.latest_delta_left,
          diagnostics.latest_delta_top, diagnostics.latest_delta_right, diagnostics.latest_delta_bottom,
          static_cast<unsigned long long>(diagnostics.latest_capture_to_submit_ns)
#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
          , static_cast<unsigned long long>(lower_diagnostics.rejected_frames),
          static_cast<unsigned long long>(lower_diagnostics.idle_queries),
          static_cast<unsigned long long>(lower_diagnostics.idle_busy_retries),
          static_cast<unsigned long long>(lower_diagnostics.idle_ready),
          static_cast<unsigned long long>(lower_diagnostics.idle_timeouts),
          static_cast<unsigned long long>(lower_diagnostics.completion_unknown_submissions),
          static_cast<unsigned long long>(lower_diagnostics.completion_timeout_observations),
          static_cast<unsigned long long>(front_light_diagnostics.polls),
          static_cast<unsigned long long>(front_light_diagnostics.polls_skipped),
          static_cast<unsigned long long>(front_light_diagnostics.poll_interval_rejections),
          static_cast<unsigned long long>(front_light_diagnostics.request_generations),
          static_cast<unsigned long long>(front_light_diagnostics.valid_resolutions),
          static_cast<unsigned long long>(front_light_diagnostics.capped_resolutions),
          static_cast<unsigned long long>(front_light_diagnostics.controller_applies),
          static_cast<unsigned long long>(front_light_diagnostics.zero_off_applies),
          static_cast<unsigned long long>(front_light_diagnostics.write_failures),
          static_cast<unsigned long long>(front_light_diagnostics.debug_control_rejections),
          static_cast<unsigned long long>(front_light_diagnostics.latest_observed_to_apply_ns)
#endif
  );
  if (snapshot_size > 0 && static_cast<std::size_t>(snapshot_size) < sizeof(snapshot)) {
    PublishSnapshot(snapshot);
  }
  ALOGI("M3 snapshot stats: copy_attempted=%llu copied=%llu skipped_retained=%llu late_sample=%llu "
        "disarmed=%llu pool_busy=%zu pool_drops=%llu "
        "r1_source=%s audit=%d source_copy=%llu/%llu/%llu/%llu/%llu "
        "source_audit=%llu/%llu/%llu/%llu/%llu direct_selected=%llu "
        "source_audit_last=%llu/%d,%d,%d,%d "
        "copy_failures=%llu accepted=%llu coalesced=%llu capture_interval_ns=%llu/%llu/%llu "
        "converted=%llu conversion_failures=%llu enqueued=%llu enqueue_rejected=%llu "
        "adapter_enqueues=%llu adapter_replaced=%llu adapter_deferred=%llu adapter_trailing=%llu "
        "adapter_submit_calls=%llu adapter_submit_rejected=%llu "
        "unchanged=%llu full_session=%llu full_rearm=%llu full_changed=%llu "
        "normal_differential=%llu/%llu/%llu "
        "resample=%llu/%llu/%llu/%llu/%llu/%llu/%llu release=%llu request_failures=%llu "
        "pressure_deferred=%llu/%llu pressure_resample_attempts=%llu "
        "delta_samples=%llu delta_unavailable=%llu delta_last=%llu/%llu/%u/%d,%d,%d,%d "
        "delta_buckets=%llu/%llu/%llu/%llu/%llu "
        "submit_accepted=%llu submit_rejected=%llu "
#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
        "lower_state=%d lower_submissions=%llu lower_rejected=%llu lower_disarmed=%llu "
        "lower_init=%d lower_update=%d lower_pending=%llu lower_completion_unknown=%llu "
        "lower_pending_unknown=%d lower_timeout_observations=%llu lower_timeout_elapsed_ns=%llu "
        "lower_idle=%llu/%llu/%llu/%llu "
        "light_state=%d light_polls=%llu light_preflight=0x%x light_observed=%llu "
        "light_applied=%llu light_write_failures=%llu light_control_rejects=%llu "
        "light_arm_transitions=%llu light_armed=%d light_blocked=%d "
        "light_optional_secondary_absent=%u "
#else
        "capture_only_submissions=%llu "
#endif
        "gray_bytes=%llu gray_fnv64=%016llx convert_ns=%llu capture_to_convert_ns=%llu "
        "capture_to_enqueue_ns=%llu engine_submit_ns=%llu enqueue_to_submit_ns=%llu "
        "capture_to_submit_ns=%llu",
        static_cast<unsigned long long>(m3_->copy_attempted),
        static_cast<unsigned long long>(m3_->copy_submitted),
        static_cast<unsigned long long>(m3_->copy_skipped_retained),
        static_cast<unsigned long long>(m3_->late_sample_taken),
        static_cast<unsigned long long>(m3_->disarmed),
        m3_->snapshot_pool->busy_count(),
        static_cast<unsigned long long>(m3_->snapshot_pool->drop_count()),
        m3_->r1_source_mode == R1SourceMode::kDirect ? "direct" : "snapshot",
        m3_->r1_source_audit ? 1 : 0,
        static_cast<unsigned long long>(m3_->r1_source_copy_attempted),
        static_cast<unsigned long long>(m3_->r1_source_copy_completed),
        static_cast<unsigned long long>(m3_->r1_source_fence_not_ready),
        static_cast<unsigned long long>(m3_->r1_source_copy_failed),
        static_cast<unsigned long long>(m3_->r1_source_direct_no_output),
        static_cast<unsigned long long>(diagnostics.source_audit_samples),
        static_cast<unsigned long long>(diagnostics.source_audit_exact_matches),
        static_cast<unsigned long long>(diagnostics.source_audit_mismatches),
        static_cast<unsigned long long>(diagnostics.source_audit_incompatible),
        static_cast<unsigned long long>(diagnostics.source_audit_unavailable),
        static_cast<unsigned long long>(diagnostics.source_direct_selected),
        static_cast<unsigned long long>(diagnostics.latest_source_audit_differing_pixels),
        diagnostics.latest_source_audit_left, diagnostics.latest_source_audit_top,
        diagnostics.latest_source_audit_right, diagnostics.latest_source_audit_bottom,
        static_cast<unsigned long long>(m3_->snapshot_pool->failed_submission_count()),
        static_cast<unsigned long long>(diagnostics.captures_accepted),
        static_cast<unsigned long long>(diagnostics.captures_coalesced),
        static_cast<unsigned long long>(diagnostics.latest_capture_interval_ns),
        static_cast<unsigned long long>(average_capture_interval_ns),
        static_cast<unsigned long long>(diagnostics.max_capture_interval_ns),
        static_cast<unsigned long long>(diagnostics.conversion_successes),
        static_cast<unsigned long long>(diagnostics.conversion_failures),
        static_cast<unsigned long long>(diagnostics.frames_enqueued),
        static_cast<unsigned long long>(diagnostics.frames_enqueue_rejected),
        static_cast<unsigned long long>(adapter_diagnostics.enqueues_accepted),
        static_cast<unsigned long long>(adapter_diagnostics.pending_replaced),
        static_cast<unsigned long long>(adapter_diagnostics.trailing_deferred),
        static_cast<unsigned long long>(adapter_diagnostics.trailing_submitted),
        static_cast<unsigned long long>(adapter_diagnostics.submit_calls),
        static_cast<unsigned long long>(adapter_diagnostics.submit_rejected),
        static_cast<unsigned long long>(diagnostics.frames_suppressed_unchanged),
        static_cast<unsigned long long>(diagnostics.frames_selected_session_start),
        static_cast<unsigned long long>(diagnostics.frames_selected_rearm),
        static_cast<unsigned long long>(diagnostics.frames_selected_content_change),
        static_cast<unsigned long long>(diagnostics.normal_differential_selected),
        static_cast<unsigned long long>(diagnostics.normal_differential_accepted),
        static_cast<unsigned long long>(diagnostics.normal_differential_rejected),
        static_cast<unsigned long long>(resample_diagnostics.created),
        static_cast<unsigned long long>(resample_diagnostics.coalesced),
        static_cast<unsigned long long>(resample_diagnostics.dispatched),
        static_cast<unsigned long long>(resample_diagnostics.expiry_fallback),
        static_cast<unsigned long long>(resample_diagnostics.fulfilled),
        static_cast<unsigned long long>(resample_diagnostics.copy_failures),
        static_cast<unsigned long long>(resample_diagnostics.cancelled),
        static_cast<unsigned long long>(resample_bridge_diagnostics.release_notifications),
        static_cast<unsigned long long>(resample_bridge_diagnostics.request_failures),
        static_cast<unsigned long long>(pressure_diagnostics.full_pool_deferred),
        static_cast<unsigned long long>(
                pressure_diagnostics.full_pool_deferred_conversion_pending),
        static_cast<unsigned long long>(pressure_diagnostics.resample_attempts),
        static_cast<unsigned long long>(diagnostics.delta_samples),
        static_cast<unsigned long long>(diagnostics.delta_unavailable),
        static_cast<unsigned long long>(diagnostics.latest_delta_changed_pixels),
        static_cast<unsigned long long>(diagnostics.latest_delta_total_pixels),
        diagnostics.latest_delta_coverage_ppm, diagnostics.latest_delta_left,
        diagnostics.latest_delta_top, diagnostics.latest_delta_right, diagnostics.latest_delta_bottom,
        static_cast<unsigned long long>(diagnostics.delta_no_change),
        static_cast<unsigned long long>(diagnostics.delta_under_one_percent),
        static_cast<unsigned long long>(diagnostics.delta_under_five_percent),
        static_cast<unsigned long long>(diagnostics.delta_under_twenty_five_percent),
        static_cast<unsigned long long>(diagnostics.delta_twenty_five_percent_or_more),
        static_cast<unsigned long long>(diagnostics.submit_acceptances),
        static_cast<unsigned long long>(diagnostics.submit_rejections),
#if defined(NEO2_EINK_M4_LOWER_ENGINE_OUTPUT)
        static_cast<int>(lower_diagnostics.state),
        static_cast<unsigned long long>(lower_diagnostics.submissions),
        static_cast<unsigned long long>(lower_diagnostics.rejected_frames),
        static_cast<unsigned long long>(lower_diagnostics.disarmed_frames),
        lower_diagnostics.initializer_result, lower_diagnostics.update_result,
        static_cast<unsigned long long>(lower_diagnostics.pending_submission_id),
        static_cast<unsigned long long>(lower_diagnostics.completion_unknown_submissions),
        lower_diagnostics.pending_completion_unknown,
        static_cast<unsigned long long>(lower_diagnostics.completion_timeout_observations),
        static_cast<unsigned long long>(lower_diagnostics.pending_timeout_elapsed_ns),
        static_cast<unsigned long long>(lower_diagnostics.idle_queries),
        static_cast<unsigned long long>(lower_diagnostics.idle_busy_retries),
        static_cast<unsigned long long>(lower_diagnostics.idle_ready),
        static_cast<unsigned long long>(lower_diagnostics.idle_timeouts),
        static_cast<int>(front_light_diagnostics.state),
        static_cast<unsigned long long>(front_light_diagnostics.polls),
        front_light_diagnostics.preflight_failures,
        static_cast<unsigned long long>(front_light_diagnostics.observed_requests),
        static_cast<unsigned long long>(front_light_diagnostics.applied_requests),
        static_cast<unsigned long long>(front_light_diagnostics.write_failures),
        static_cast<unsigned long long>(front_light_diagnostics.debug_control_rejections),
        static_cast<unsigned long long>(front_light_diagnostics.arm_transitions),
        front_light_diagnostics.write_armed, front_light_diagnostics.writes_blocked,
        front_light_diagnostics.optional_secondary_absent,
#else
        static_cast<unsigned long long>(m3_->engine.SubmissionCount()),
#endif
        static_cast<unsigned long long>(diagnostics.latest_grayscale_bytes),
        static_cast<unsigned long long>(diagnostics.latest_grayscale_hash),
        static_cast<unsigned long long>(diagnostics.latest_convert_ns),
        static_cast<unsigned long long>(diagnostics.latest_capture_to_convert_ns),
        static_cast<unsigned long long>(diagnostics.latest_capture_to_enqueue_ns),
        static_cast<unsigned long long>(diagnostics.latest_engine_submit_ns),
        static_cast<unsigned long long>(diagnostics.latest_enqueue_to_submit_ns),
        static_cast<unsigned long long>(diagnostics.latest_capture_to_submit_ns));
}

}  // namespace neo2::eink::android
