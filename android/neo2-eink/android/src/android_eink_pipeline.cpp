#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "Neo2EinkCapture"

#include "neo2/eink/android_eink_pipeline.h"

#include <eink_grayscale_fidelity.h>
#include <eink_sleep_image_background.h>
#include <eink_sleep_image_overlay.h>
#include <eink_software_grayscale.h>

#include <android-base/properties.h>

#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <log/log.h>
#include <algorithm>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace neo2::eink::android {

namespace {

constexpr char kSleepCycleTraceProperty[] = "debug.neo2.eink.sleep_trace";

bool SleepCycleTraceEnabled() {
  return ::android::base::GetBoolProperty(kSleepCycleTraceProperty, false);
}

std::uint64_t FingerprintGrayscale(const OwnedGrayscaleBuffer& grayscale) {
  constexpr std::uint64_t kOffset = 1469598103934665603ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t hash = kOffset;
  const std::uint8_t* bytes = grayscale.Data();
  for (std::size_t index = 0; index < grayscale.Size(); ++index) {
    hash ^= bytes[index];
    hash *= kPrime;
  }
  return hash;
}

std::uint64_t ToNanoseconds(std::chrono::steady_clock::duration duration) {
  return static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

void RecordDeltaDiagnostics(AndroidEinkPipeline::Diagnostics* diagnostics,
                            const std::optional<GrayscaleDelta>& delta,
                            bool delta_unavailable) {
  if (delta_unavailable) {
    ++diagnostics->delta_unavailable;
    return;
  }
  if (!delta) return;
  ++diagnostics->delta_samples;
  diagnostics->latest_delta_changed_pixels = delta->changed_pixels;
  diagnostics->latest_delta_total_pixels = delta->total_pixels;
  diagnostics->latest_delta_coverage_ppm = delta->CoveragePpm();
  diagnostics->latest_delta_left = delta->left;
  diagnostics->latest_delta_top = delta->top;
  diagnostics->latest_delta_right = delta->right;
  diagnostics->latest_delta_bottom = delta->bottom;
  switch (ClassifyDeltaCoverage(*delta)) {
    case DeltaCoverageBucket::kNoChange:
      ++diagnostics->delta_no_change;
      break;
    case DeltaCoverageBucket::kUnderOnePercent:
      ++diagnostics->delta_under_one_percent;
      break;
    case DeltaCoverageBucket::kUnderFivePercent:
      ++diagnostics->delta_under_five_percent;
      break;
    case DeltaCoverageBucket::kUnderTwentyFivePercent:
      ++diagnostics->delta_under_twenty_five_percent;
      break;
    case DeltaCoverageBucket::kTwentyFivePercentOrMore:
      ++diagnostics->delta_twenty_five_percent_or_more;
      break;
  }
}

void RecordTerminalBlackDiagnostics(AndroidEinkPipeline::Diagnostics* diagnostics,
                                    TerminalBlackOutcome outcome) {
  switch (outcome) {
    case TerminalBlackOutcome::kCandidateReplaced:
      ++diagnostics->terminal_black_candidates;
      ++diagnostics->terminal_black_catalog_ready;
      break;
    case TerminalBlackOutcome::kCatalogMissingFallback:
      ++diagnostics->terminal_black_candidates;
      ++diagnostics->terminal_black_catalog_missing;
      ++diagnostics->terminal_black_last_real_holds;
      break;
    case TerminalBlackOutcome::kDuplicateSuppressed:
      ++diagnostics->terminal_black_candidates;
      ++diagnostics->terminal_black_duplicate_suppressions;
      ++diagnostics->sleep_image_suppressions;
      break;
    case TerminalBlackOutcome::kRearmed:
      ++diagnostics->terminal_black_rearm_events;
      break;
    case TerminalBlackOutcome::kNone:
      break;
  }
}

}  // namespace

class AndroidEinkPipeline::Impl {
 public:
  std::mutex mutex;
  std::condition_variable condition;
  std::optional<CapturedComposition> pending;
  bool conversion_in_flight = false;
  std::thread conversion_worker;
  std::uint64_t next_sequence = 1;
  Diagnostics diagnostics;
  FrameDemandGate demand_gate;
  std::vector<std::uint8_t> previous_grayscale;
  int previous_grayscale_width = 0;
  int previous_grayscale_height = 0;
  bool has_previous_grayscale = false;
  std::chrono::steady_clock::time_point previous_capture_at{};
  bool reset_demand_gate = false;
  bool reset_source_state = false;
  std::uint64_t source_generation = 0;
  bool has_been_enabled = false;
  bool enabled = false;
  bool started = false;
  bool stopping = false;
  SleepImageLatch sleep_image_latch;
  SleepImageBackground sleep_image_background{EinkSleepImageCatalog::kPanelWidth,
                                               EinkSleepImageCatalog::kPanelHeight};
  std::shared_ptr<EinkSleepImageCatalog> sleep_image_catalog;
  std::uint64_t sleep_image_catalog_epoch = 0;
  std::size_t sleep_image_selected_index = 0;
  SleepImagePresentationMode sleep_image_mode = SleepImagePresentationMode::kOpaque;
};

AndroidEinkPipeline::AndroidEinkPipeline(
        Engine& engine, Rotation rotation, int engine_mode, int policy_flag,
        PresentationAdapter::SubmissionIntervalProvider interval_provider)
    : engine_(engine),
      rotation_(rotation),
      engine_mode_(engine_mode),
      policy_flag_(policy_flag),
      adapter_(engine_, [this](const Frame& frame, bool accepted,
                               std::chrono::steady_clock::duration submit_duration) {
        OnEngineSubmission(frame, accepted, submit_duration);
      }, std::move(interval_provider)),
      impl_(std::make_unique<Impl>()) {}

AndroidEinkPipeline::~AndroidEinkPipeline() {
  Stop();
}

void AndroidEinkPipeline::Start() {
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->started || impl_->stopping) {
      return;
    }
    impl_->started = true;
  }
  adapter_.Start();
  impl_->conversion_worker = std::thread(&AndroidEinkPipeline::ConversionWorkerMain, this);
}

void AndroidEinkPipeline::Stop() {
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->started || impl_->stopping) {
      return;
    }
    impl_->stopping = true;
    impl_->enabled = false;
    impl_->pending.reset();
  }
  impl_->condition.notify_one();
  impl_->conversion_worker.join();
  adapter_.Stop();
}

void AndroidEinkPipeline::SetEnabled(bool enabled) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->started && !impl_->stopping) {
    if (impl_->enabled != enabled) {
      // A frame converted while disarmed is deliberately prevented from
      // reaching the retained engine. Do not later treat that frame as the
      // panel's baseline when presentation is rearmed.
      if (impl_->has_been_enabled) {
        impl_->reset_demand_gate = true;
      }
    }
    impl_->enabled = enabled;
    impl_->has_been_enabled = impl_->has_been_enabled || enabled;
  }
}

void AndroidEinkPipeline::ResetForSourceChange() {
  std::lock_guard lock(impl_->mutex);
  if (!impl_->started || impl_->stopping) return;
  // Destroying a pending owned snapshot releases its pool lease. An in-flight
  // conversion records the prior generation and is discarded before output;
  // only the worker clears policy/baseline state, avoiding composition-thread
  // mutation of its grayscale storage.
  ++impl_->source_generation;
  impl_->pending.reset();
  impl_->reset_source_state = true;
  impl_->sleep_image_background.Release();
}

EinkSleepImageCatalog::PublishResult AndroidEinkPipeline::PublishSleepImageCatalog(
        std::uint64_t epoch, int width, int height,
        std::vector<EinkSleepImageCatalog::Entry> entries, std::size_t selected_index,
        SleepImagePresentationMode mode) {
  if (epoch == 0) return EinkSleepImageCatalog::PublishResult::kRejectedSelection;
  if (width != EinkSleepImageCatalog::kPanelWidth ||
      height != EinkSleepImageCatalog::kPanelHeight) {
    return EinkSleepImageCatalog::PublishResult::kRejectedGeometry;
  }
  if (mode != SleepImagePresentationMode::kOpaque &&
      mode != SleepImagePresentationMode::kOverlay) {
    return EinkSleepImageCatalog::PublishResult::kRejectedSelection;
  }
  auto catalog = std::make_shared<EinkSleepImageCatalog>(width, height);
  const EinkSleepImageCatalog::PublishResult result = catalog->Publish(std::move(entries));
  if (result != EinkSleepImageCatalog::PublishResult::kAccepted) return result;
  if (selected_index >= catalog->size()) {
    return EinkSleepImageCatalog::PublishResult::kRejectedSelection;
  }
  std::lock_guard lock(impl_->mutex);
  if (impl_->stopping) {
    return EinkSleepImageCatalog::PublishResult::kRejectedSelection;
  }
  // The framework producer only publishes validated awake-state callbacks.
  // Reject stale or duplicate generations so a later Binder result cannot
  // replace the prepublished selection in an already-consumed latch cycle.
  if (epoch <= impl_->sleep_image_catalog_epoch) {
    return EinkSleepImageCatalog::PublishResult::kRejectedSelection;
  }
  impl_->sleep_image_catalog = std::move(catalog);
  impl_->sleep_image_catalog_epoch = epoch;
  impl_->sleep_image_selected_index = selected_index;
  impl_->sleep_image_mode = mode;
  impl_->diagnostics.sleep_image_mode = static_cast<std::uint8_t>(mode);
  return EinkSleepImageCatalog::PublishResult::kAccepted;
}

SleepImageArmResult AndroidEinkPipeline::ArmSleepCycle(std::uint64_t epoch) {
  std::lock_guard lock(impl_->mutex);
  SleepImageArmResult result = SleepImageArmResult::kRejectedMismatched;
  if (!impl_->stopping && epoch != 0 && epoch == impl_->sleep_image_catalog_epoch) {
    result = impl_->sleep_image_latch.ArmSleepCycle(epoch);
  }
  if (result == SleepImageArmResult::kAccepted) {
    ++impl_->diagnostics.sleep_cycle_arm_accepted;
  } else {
    ++impl_->diagnostics.sleep_cycle_arm_rejected;
  }
  impl_->diagnostics.sleep_cycle_active_epoch = impl_->sleep_image_latch.active_epoch();
  if (SleepCycleTraceEnabled()) {
    ALOGI("sleep_cycle_arm epoch=%llu result=%d active_epoch=%llu",
          static_cast<unsigned long long>(epoch), static_cast<int>(result),
          static_cast<unsigned long long>(impl_->sleep_image_latch.active_epoch()));
  }
  return result;
}

SleepImagePresentationResult AndroidEinkPipeline::PresentSleepImage(std::uint64_t epoch) {
  std::lock_guard lock(impl_->mutex);
  const auto reject = [this](SleepImagePresentationResult result) {
    ++impl_->diagnostics.sleep_image_present_rejected;
    return result;
  };
  if (epoch == 0) return reject(SleepImagePresentationResult::kRejectedZero);
  if (!impl_->started || impl_->stopping || !impl_->enabled) {
    return reject(SleepImagePresentationResult::kRejectedUnavailable);
  }
  if (!impl_->sleep_image_catalog || epoch != impl_->sleep_image_catalog_epoch) {
    return reject(SleepImagePresentationResult::kRejectedCatalog);
  }
  const SleepImageCandidate candidate =
          impl_->sleep_image_catalog->CandidateAt(impl_->sleep_image_selected_index);
  if (!candidate.buffer || candidate.width != EinkSleepImageCatalog::kPanelWidth ||
      candidate.height != EinkSleepImageCatalog::kPanelHeight ||
      candidate.buffer->ByteCount() != EinkSleepImageCatalog::kPanelBytes || !candidate.alpha ||
      candidate.alpha->size() != EinkSleepImageCatalog::kPanelBytes) {
    return reject(SleepImagePresentationResult::kRejectedCatalog);
  }

  std::shared_ptr<OwnedGrayscaleBuffer> presentation;
  const std::span foreground(candidate.buffer->Data(), candidate.buffer->ByteCount());
  const std::span alpha(*candidate.alpha);
  if (impl_->sleep_image_mode == SleepImagePresentationMode::kOverlay) {
    SleepImagePreparedOverlay prepared =
            impl_->sleep_image_background.PrepareOverlay(foreground, alpha);
    if (prepared.result == SleepImageOverlayPreparationResult::kRejectedNoBackground) {
      ++impl_->diagnostics.sleep_overlay_no_background;
      return reject(SleepImagePresentationResult::kRejectedBackground);
    }
    if (prepared.result != SleepImageOverlayPreparationResult::kComposited) {
      ++impl_->diagnostics.sleep_overlay_invalid;
      return reject(SleepImagePresentationResult::kRejectedAlpha);
    }
    ++impl_->diagnostics.sleep_overlay_composited;
    presentation = std::make_shared<OwnedGrayscaleBuffer>(
            candidate.width, candidate.height, std::move(prepared.panel_gray));
  } else {
    const std::vector<std::uint8_t> white(EinkSleepImageCatalog::kPanelBytes, 0xf0);
    SleepImageOverlayOutput prepared = ComposeSleepImageOverlay(foreground, alpha, white);
    if (prepared.result != SleepImageOverlayResult::kComposited) {
      ++impl_->diagnostics.sleep_overlay_invalid;
      return reject(SleepImagePresentationResult::kRejectedAlpha);
    }
    presentation = std::make_shared<OwnedGrayscaleBuffer>(
            candidate.width, candidate.height, std::move(prepared.panel_gray));
  }

  const SleepImageArmResult arm = impl_->sleep_image_latch.ArmSleepCycle(epoch);
  switch (arm) {
    case SleepImageArmResult::kAccepted:
      break;
    case SleepImageArmResult::kRejectedZero:
      return reject(SleepImagePresentationResult::kRejectedZero);
    case SleepImageArmResult::kRejectedStale:
      return reject(SleepImagePresentationResult::kRejectedStale);
    case SleepImageArmResult::kRejectedDuplicate:
      return reject(SleepImagePresentationResult::kRejectedDuplicate);
    case SleepImageArmResult::kRejectedMismatched:
      return reject(SleepImagePresentationResult::kRejectedMismatched);
  }
  const SleepImagePresentationResult begin = impl_->sleep_image_latch.BeginPresentation(epoch);
  if (begin != SleepImagePresentationResult::kAccepted) return reject(begin);

  // The lifecycle candidate supersedes every older capture that has not
  // reached the serial adapter. An in-flight conversion observes the changed
  // generation and discards its result before enqueue.
  ++impl_->source_generation;
  impl_->pending.reset();
  const auto now = std::chrono::steady_clock::now();
  Frame frame{
          .sequence = impl_->next_sequence++,
          .sleep_image_epoch = epoch,
          .captured_at = now,
          .enqueued_at = now,
          .grayscale_buffer = std::move(presentation),
          .dirty_rect = {.left = 0,
                         .top = 0,
                         .right = candidate.width,
                         .bottom = candidate.height},
          .engine_mode = engine_mode_,
          .policy_flag = policy_flag_,
  };
  if (!adapter_.Enqueue(std::move(frame))) {
    static_cast<void>(impl_->sleep_image_latch.DisarmSleepCycle(epoch));
    return reject(SleepImagePresentationResult::kRejectedEnqueue);
  }
  ++impl_->diagnostics.sleep_image_present_accepted;
  impl_->diagnostics.sleep_cycle_active_epoch = epoch;
  impl_->diagnostics.sleep_cycle_last_enqueue_result = 1;
  if (SleepCycleTraceEnabled()) {
    ALOGI("sleep_image_present epoch=%llu result=accepted",
          static_cast<unsigned long long>(epoch));
  }
  return SleepImagePresentationResult::kAccepted;
}

SleepImageDisarmResult AndroidEinkPipeline::DisarmSleepCycle(std::uint64_t epoch) {
  std::lock_guard lock(impl_->mutex);
  const SleepImageDisarmResult result = impl_->sleep_image_latch.DisarmSleepCycle(epoch);
  if (result == SleepImageDisarmResult::kAccepted) {
    static_cast<void>(adapter_.CancelPendingSleepImage(epoch));
    if (impl_->sleep_image_background.available()) {
      ++impl_->diagnostics.sleep_overlay_background_released;
    }
    impl_->sleep_image_background.Release();
    impl_->reset_demand_gate = true;
    ++impl_->diagnostics.sleep_cycle_disarm_accepted;
  } else {
    ++impl_->diagnostics.sleep_cycle_disarm_rejected;
  }
  impl_->diagnostics.sleep_cycle_active_epoch = impl_->sleep_image_latch.active_epoch();
  if (SleepCycleTraceEnabled()) {
    ALOGI("sleep_cycle_disarm epoch=%llu result=%d active_epoch=%llu",
          static_cast<unsigned long long>(epoch), static_cast<int>(result),
          static_cast<unsigned long long>(impl_->sleep_image_latch.active_epoch()));
  }
  return result;
}

void AndroidEinkPipeline::OnCapturedComposition(CapturedComposition composition) {
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->started || impl_->stopping || !impl_->enabled) {
      return;
    }
    if (impl_->sleep_image_latch.state() == SleepImageLatchState::kConsumed) {
      ++impl_->diagnostics.sleep_image_capture_holds;
      return;
    }
    // Replace the oldest not-yet-converted composition. sp<> and Fence own
    // their resources; a pool-owned composition's RAII lease also returns its
    // destination slot when this assignment destroys the older pending value.
    if (impl_->pending.has_value()) {
      ++impl_->diagnostics.captures_coalesced;
    }
    if (impl_->previous_capture_at != std::chrono::steady_clock::time_point{} &&
        composition.captured_at > impl_->previous_capture_at) {
      const auto interval = ToNanoseconds(composition.captured_at - impl_->previous_capture_at);
      ++impl_->diagnostics.capture_interval_samples;
      impl_->diagnostics.capture_interval_total_ns += interval;
      impl_->diagnostics.latest_capture_interval_ns = interval;
      impl_->diagnostics.max_capture_interval_ns =
              std::max(impl_->diagnostics.max_capture_interval_ns, interval);
    }
    impl_->previous_capture_at = composition.captured_at;
    impl_->pending = std::move(composition);
    ++impl_->diagnostics.captures_accepted;
  }
  impl_->condition.notify_one();
}

AndroidEinkPipeline::Diagnostics AndroidEinkPipeline::diagnostics() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->diagnostics;
}

PresentationAdapter::Diagnostics AndroidEinkPipeline::adapter_diagnostics() const {
  return adapter_.diagnostics();
}

AndroidEinkPipeline::CaptureAdmissionState AndroidEinkPipeline::capture_admission_state() const {
  std::lock_guard lock(impl_->mutex);
  const auto output_slot = adapter_.output_slot_state();
  const bool pending_capture = impl_->pending.has_value();
  const bool conversion_in_flight = impl_->conversion_in_flight;
  const bool adapter_retained = output_slot.has_retained_candidate;
  return {.retained_candidate = pending_capture || conversion_in_flight || adapter_retained,
          .conversion_in_flight = conversion_in_flight,
          .pending_capture = pending_capture,
          .adapter_retained = adapter_retained,
          .next_eligible = output_slot.next_eligible};
}

void AndroidEinkPipeline::OnEngineSubmission(
        const Frame& frame, bool accepted, std::chrono::steady_clock::duration submit_duration) {
  const auto submit_finish = std::chrono::steady_clock::now();
  // Lifecycle paths take the pipeline lock before retaining or releasing page pixels. Preserve
  // that order here so wake/reset cannot race a stale submission or deadlock with this callback.
  std::lock_guard lock(impl_->mutex);
  const SleepImageBackgroundResult background =
          impl_->sleep_image_background.ObserveSubmission(frame, accepted);
  if (background == SleepImageBackgroundResult::kRetained) {
    ++impl_->diagnostics.sleep_overlay_background_retained;
  } else {
    ++impl_->diagnostics.sleep_overlay_background_rejected;
  }
  if (accepted) {
    ++impl_->diagnostics.submit_acceptances;
  } else {
    ++impl_->diagnostics.submit_rejections;
  }
  impl_->diagnostics.latest_engine_submit_ns = ToNanoseconds(submit_duration);
  if (frame.enqueued_at != std::chrono::steady_clock::time_point{}) {
    impl_->diagnostics.latest_enqueue_to_submit_ns =
            ToNanoseconds(submit_finish - frame.enqueued_at);
  }
  if (frame.captured_at != std::chrono::steady_clock::time_point{}) {
    impl_->diagnostics.latest_capture_to_submit_ns =
            ToNanoseconds(submit_finish - frame.captured_at);
  }
  if (frame.engine_mode == engine_mode_) {
    if (accepted) {
      ++impl_->diagnostics.normal_differential_accepted;
    } else {
      ++impl_->diagnostics.normal_differential_rejected;
    }
  }
  if (SleepCycleTraceEnabled()) {
    ALOGI("sleep_frame_lower sequence=%llu result=%s",
          static_cast<unsigned long long>(frame.sequence), accepted ? "accepted" : "rejected");
  }
}

void AndroidEinkPipeline::ConversionWorkerMain() {
  while (true) {
    std::optional<CapturedComposition> composition;
    std::uint64_t sequence = 0;
    std::uint64_t source_generation = 0;
    {
      std::unique_lock lock(impl_->mutex);
      impl_->condition.wait(lock, [this] { return impl_->stopping || impl_->pending.has_value(); });
      if (impl_->stopping) {
        return;
      }
      composition = std::move(impl_->pending);
      impl_->pending.reset();
      impl_->conversion_in_flight = true;
      sequence = impl_->next_sequence++;
      source_generation = impl_->source_generation;
      if (impl_->reset_source_state) {
        impl_->demand_gate.Reset();
        impl_->previous_grayscale.clear();
        impl_->previous_grayscale_width = 0;
        impl_->previous_grayscale_height = 0;
        impl_->has_previous_grayscale = false;
        impl_->previous_capture_at = {};
        impl_->reset_source_state = false;
      }
      if (impl_->reset_demand_gate) {
        impl_->demand_gate.Reset();
        impl_->reset_demand_gate = false;
      }
    }

    const auto conversion_start = std::chrono::steady_clock::now();
    const auto snapshot_grayscale = converter_.Convert(*composition, rotation_);
    const auto conversion_finish = std::chrono::steady_clock::now();
    const auto convert_ns = ToNanoseconds(conversion_finish - conversion_start);
    if (!snapshot_grayscale ||
        (composition->direct_grayscale_selected && !composition->direct_grayscale)) {
      const auto capture_to_convert_ns =
              ToNanoseconds(conversion_finish - composition->captured_at);
      bool first_failure = false;
      {
        std::lock_guard lock(impl_->mutex);
        ++impl_->diagnostics.conversion_failures;
        ++impl_->diagnostics.conversion_duration_samples;
        impl_->diagnostics.conversion_duration_total_ns += convert_ns;
        impl_->diagnostics.conversion_duration_max_ns =
                std::max(impl_->diagnostics.conversion_duration_max_ns, convert_ns);
        impl_->conversion_in_flight = false;
        first_failure = impl_->diagnostics.conversion_failures == 1;
        impl_->diagnostics.latest_convert_ns = convert_ns;
        impl_->diagnostics.latest_capture_to_convert_ns = capture_to_convert_ns;
      }
      if (first_failure) {
        ALOGW("M3 first owned snapshot conversion failed: convert_ns=%llu capture_to_convert_ns=%llu",
              static_cast<unsigned long long>(convert_ns),
              static_cast<unsigned long long>(capture_to_convert_ns));
      }
      continue;
    }
    {
      std::lock_guard lock(impl_->mutex);
      if (source_generation != impl_->source_generation) {
        impl_->conversion_in_flight = false;
        continue;
      }
    }
    if (composition->direct_grayscale_audit_requested) {
      std::lock_guard lock(impl_->mutex);
      if (!composition->direct_grayscale) {
        ++impl_->diagnostics.source_audit_unavailable;
      } else {
        const auto fidelity = CompareGrayscaleFidelity(*composition->direct_grayscale,
                                                       *snapshot_grayscale);
        ++impl_->diagnostics.source_audit_samples;
        if (!fidelity.comparable) {
          ++impl_->diagnostics.source_audit_incompatible;
        } else if (fidelity.exact_match) {
          ++impl_->diagnostics.source_audit_exact_matches;
        } else {
          ++impl_->diagnostics.source_audit_mismatches;
          impl_->diagnostics.latest_source_audit_differing_pixels =
                  fidelity.differing_pixels;
          impl_->diagnostics.latest_source_audit_left = fidelity.left;
          impl_->diagnostics.latest_source_audit_top = fidelity.top;
          impl_->diagnostics.latest_source_audit_right = fidelity.right;
          impl_->diagnostics.latest_source_audit_bottom = fidelity.bottom;
        }
      }
    }
    const auto& grayscale = composition->direct_grayscale_selected
            ? composition->direct_grayscale
            : snapshot_grayscale;
    if (composition->direct_grayscale_selected) {
      std::lock_guard lock(impl_->mutex);
      ++impl_->diagnostics.source_direct_selected;
    }
    std::size_t selected_index = 0;
    std::shared_ptr<EinkSleepImageCatalog> catalog;
    Frame sleep_frame{
            .sequence = sequence,
            .captured_at = composition->captured_at,
            .grayscale_buffer = grayscale,
            .dirty_rect = {.left = 0,
                           .top = 0,
                           .right = grayscale->Width(),
                           .bottom = grayscale->Height()},
            .engine_mode = engine_mode_,
            .policy_flag = policy_flag_,
    };
    SleepImageDecision sleep_decision = SleepImageDecision::kPassThrough;
    TerminalBlackOutcome terminal_black_outcome = TerminalBlackOutcome::kNone;
    SleepImageLatchState latch_state_before = SleepImageLatchState::kInactive;
    SleepImageEvaluationSummary sleep_evaluation;
    std::uint64_t sleep_cycle_epoch = 0;
    {
      std::lock_guard lock(impl_->mutex);
      selected_index = impl_->sleep_image_selected_index;
      catalog = impl_->sleep_image_catalog;
      SleepImageLatch::Selector sleep_selector;
      if (catalog) {
        sleep_selector = [catalog = std::move(catalog), selected_index](int width, int height) {
          const SleepImageCandidate candidate = catalog->CandidateAt(selected_index);
          if (candidate.width != width || candidate.height != height) return SleepImageCandidate{};
          return candidate;
        };
      }
      latch_state_before = impl_->sleep_image_latch.state();
      sleep_decision = impl_->sleep_image_latch.Evaluate(sleep_frame, sleep_selector);
      sleep_evaluation = impl_->sleep_image_latch.last_evaluation();
      terminal_black_outcome = impl_->sleep_image_latch.last_outcome();
      RecordTerminalBlackDiagnostics(&impl_->diagnostics, terminal_black_outcome);
      impl_->diagnostics.sleep_cycle_active_epoch = impl_->sleep_image_latch.active_epoch();
      sleep_cycle_epoch = impl_->diagnostics.sleep_cycle_active_epoch;
      impl_->diagnostics.sleep_cycle_last_conversion_sequence = sequence;
      impl_->diagnostics.sleep_cycle_latch_state_before_decision =
              static_cast<std::uint8_t>(latch_state_before);
      impl_->diagnostics.sleep_cycle_last_decision = static_cast<std::uint8_t>(sleep_decision);
    }
    if (SleepCycleTraceEnabled() && sleep_evaluation.valid_full_frame) {
      ALOGI("sleep_frame_evaluated sequence=%llu min=%u max=%u mean=%u threshold=%u "
            "ready=%d holding=%d terminal_decision=%d",
            static_cast<unsigned long long>(sequence),
            static_cast<unsigned>(sleep_evaluation.minimum_gray),
            static_cast<unsigned>(sleep_evaluation.maximum_gray),
            static_cast<unsigned>(sleep_evaluation.mean_gray),
            static_cast<unsigned>(sleep_evaluation.threshold), sleep_evaluation.ready,
            sleep_evaluation.holding,
            static_cast<int>(sleep_evaluation.decision));
    }
    const std::uint64_t grayscale_hash = FingerprintGrayscale(*grayscale);
    const std::uint64_t capture_to_convert_ns =
            ToNanoseconds(conversion_finish - composition->captured_at);
    std::optional<GrayscaleDelta> delta;
    bool delta_unavailable = false;
    if (impl_->has_previous_grayscale) {
      if (impl_->previous_grayscale_width == grayscale->Width() &&
          impl_->previous_grayscale_height == grayscale->Height() &&
          impl_->previous_grayscale.size() == grayscale->Size()) {
        delta = MeasureGrayscaleDelta(impl_->previous_grayscale.data(), grayscale->Data(),
                                      grayscale->Size(), grayscale->Width(), grayscale->Height());
        delta_unavailable = !delta.has_value();
      } else {
        delta_unavailable = true;
      }
    }
    impl_->previous_grayscale.assign(grayscale->Data(), grayscale->Data() + grayscale->Size());
    impl_->previous_grayscale_width = grayscale->Width();
    impl_->previous_grayscale_height = grayscale->Height();
    impl_->has_previous_grayscale = true;
    const auto demand = impl_->demand_gate.Evaluate({
            .hash = grayscale_hash,
            .bytes = grayscale->Size(),
            .width = grayscale->Width(),
            .height = grayscale->Height(),
    });

    if (sleep_decision == SleepImageDecision::kSuppress) {
      std::lock_guard lock(impl_->mutex);
      ++impl_->diagnostics.conversion_successes;
      ++impl_->diagnostics.conversion_duration_samples;
      impl_->diagnostics.conversion_duration_total_ns += convert_ns;
      impl_->diagnostics.conversion_duration_max_ns =
              std::max(impl_->diagnostics.conversion_duration_max_ns, convert_ns);
      impl_->conversion_in_flight = false;
      RecordDeltaDiagnostics(&impl_->diagnostics, delta, delta_unavailable);
      impl_->diagnostics.latest_grayscale_hash = grayscale_hash;
      impl_->diagnostics.latest_grayscale_bytes = grayscale->Size();
      impl_->diagnostics.latest_convert_ns = ToNanoseconds(conversion_finish - conversion_start);
      impl_->diagnostics.latest_capture_to_convert_ns = capture_to_convert_ns;
      impl_->diagnostics.sleep_cycle_last_enqueue_result = 2;
      if (SleepCycleTraceEnabled()) {
        ALOGI("sleep_cycle_decision epoch=%llu latch_before=%d decision=%d sequence=%llu enqueue=held",
              static_cast<unsigned long long>(sleep_cycle_epoch),
              static_cast<int>(latch_state_before), static_cast<int>(sleep_decision),
              static_cast<unsigned long long>(sequence));
      }
      continue;
    }

    if (demand == DemandDecision::kUnchanged && sleep_decision != SleepImageDecision::kReplace) {
      std::lock_guard lock(impl_->mutex);
      ++impl_->diagnostics.conversion_successes;
      ++impl_->diagnostics.conversion_duration_samples;
      impl_->diagnostics.conversion_duration_total_ns += convert_ns;
      impl_->diagnostics.conversion_duration_max_ns =
              std::max(impl_->diagnostics.conversion_duration_max_ns, convert_ns);
      impl_->conversion_in_flight = false;
      ++impl_->diagnostics.frames_suppressed_unchanged;
      RecordDeltaDiagnostics(&impl_->diagnostics, delta, delta_unavailable);
      impl_->diagnostics.latest_grayscale_hash = grayscale_hash;
      impl_->diagnostics.latest_grayscale_bytes = grayscale->Size();
      impl_->diagnostics.latest_convert_ns = ToNanoseconds(conversion_finish - conversion_start);
      impl_->diagnostics.latest_capture_to_convert_ns = capture_to_convert_ns;
      continue;
    }

    // An owned snapshot has no reason to remain in the serial adapter after
    // conversion: CaptureOnlyEngine consumes only metadata plus owned gray
    // bytes. Releasing the composition here returns the generation-matched GPU
    // pool slot before a later frame can occupy it.
    const bool is_owned_snapshot = static_cast<bool>(composition->ownership_lease);
    Frame frame{
            .sequence = sequence,
            .sleep_image_epoch = sleep_decision == SleepImageDecision::kReplace
                    ? sleep_cycle_epoch
                    : 0,
            .captured_at = composition->captured_at,
            .compose_buffer = is_owned_snapshot
                    ? nullptr
                    : std::make_shared<AndroidComposeBuffer>(
                              std::move(composition->compose_buffer),
                              std::move(composition->ready_fence)),
            .grayscale_buffer = sleep_frame.grayscale_buffer,
            .dirty_rect = {.left = 0,
                           .top = 0,
                           .right = grayscale->Width(),
                           .bottom = grayscale->Height()},
            .engine_mode = engine_mode_,
            .policy_flag = policy_flag_,
    };
    composition.reset();
    frame.enqueued_at = std::chrono::steady_clock::now();
    const auto capture_to_enqueue_ns = ToNanoseconds(frame.enqueued_at - frame.captured_at);
    {
      std::lock_guard lock(impl_->mutex);
      ++impl_->diagnostics.normal_differential_selected;
    }
    const bool enqueued = adapter_.Enqueue(std::move(frame));
    bool first_success = false;
    {
      std::lock_guard lock(impl_->mutex);
      ++impl_->diagnostics.conversion_successes;
      ++impl_->diagnostics.conversion_duration_samples;
      impl_->diagnostics.conversion_duration_total_ns += convert_ns;
      impl_->diagnostics.conversion_duration_max_ns =
              std::max(impl_->diagnostics.conversion_duration_max_ns, convert_ns);
      impl_->conversion_in_flight = false;
      first_success = impl_->diagnostics.conversion_successes == 1;
      RecordDeltaDiagnostics(&impl_->diagnostics, delta, delta_unavailable);
      impl_->diagnostics.latest_grayscale_hash = grayscale_hash;
      impl_->diagnostics.latest_grayscale_bytes = grayscale->Size();
      impl_->diagnostics.latest_convert_ns = convert_ns;
      impl_->diagnostics.latest_capture_to_convert_ns = capture_to_convert_ns;
      impl_->diagnostics.latest_capture_to_enqueue_ns = capture_to_enqueue_ns;
      if (enqueued) {
        ++impl_->diagnostics.frames_enqueued;
      } else {
        ++impl_->diagnostics.frames_enqueue_rejected;
      }
      impl_->diagnostics.sleep_cycle_last_enqueue_result = enqueued ? 1 : 0;
      if (sleep_decision == SleepImageDecision::kReplace) {
        ++impl_->diagnostics.sleep_image_replacements;
      }
      switch (demand) {
        case DemandDecision::kSessionStart:
          ++impl_->diagnostics.frames_selected_session_start;
          break;
        case DemandDecision::kRearmed:
          ++impl_->diagnostics.frames_selected_rearm;
          break;
        case DemandDecision::kPanelContentChanged:
          ++impl_->diagnostics.frames_selected_content_change;
          break;
        case DemandDecision::kUnchanged:
          break;
      }
    }
    if (SleepCycleTraceEnabled()) {
      ALOGI("sleep_cycle_decision epoch=%llu latch_before=%d decision=%d sequence=%llu enqueue=%d",
            static_cast<unsigned long long>(sleep_cycle_epoch),
            static_cast<int>(latch_state_before), static_cast<int>(sleep_decision),
            static_cast<unsigned long long>(sequence), enqueued ? 1 : 0);
    }
    if (first_success) {
      ALOGI("M3 first panel-visible snapshot conversion complete: gray_bytes=%zu gray_fnv64=%016llx "
            "convert_ns=%llu capture_to_convert_ns=%llu enqueued=%d",
            grayscale->Size(), static_cast<unsigned long long>(grayscale_hash),
            static_cast<unsigned long long>(convert_ns),
            static_cast<unsigned long long>(capture_to_convert_ns), enqueued);
    }
  }
}

}  // namespace neo2::eink::android
