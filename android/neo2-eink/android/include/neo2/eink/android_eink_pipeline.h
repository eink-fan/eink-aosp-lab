#pragma once

#include "neo2/eink/android_grayscale_converter.h"
#include "neo2/eink/eink_sleep_image_catalog.h"
#include "neo2/eink/render_surface_capture.h"

#include <eink_frame_demand_gate.h>
#include <eink_grayscale_delta.h>
#include <eink_presentation_adapter.h>
#include <eink_sleep_image_latch.h>

#include <ui/GraphicBuffer.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace neo2::eink::android {

// Android Engine implementations recover the exact captured GraphicBuffer
// through this type when submitting to the vendor wrapper.
class AndroidComposeBuffer final : public ComposeBuffer {
 public:
  static constexpr std::uint32_t kTypeTag = 0x4e324742;  // "N2GB"

  AndroidComposeBuffer(::android::sp<::android::GraphicBuffer> buffer,
                       ::android::sp<::android::Fence> ready_fence,
                       std::shared_ptr<void> ownership_lease = nullptr)
      : buffer_(std::move(buffer)),
        ready_fence_(std::move(ready_fence)),
        ownership_lease_(std::move(ownership_lease)) {}

  [[nodiscard]] const ::android::sp<::android::GraphicBuffer>& buffer() const {
    return buffer_;
  }

  // The fence is duplicated by RenderSurfaceCapture before queueBuffer. A
  // future SyncControllerBackend passes it unchanged to the vendor wrapper;
  // the current CPU staging path consumes it in AndroidGrayscaleConverter.
  [[nodiscard]] const ::android::sp<::android::Fence>& ready_fence() const {
    return ready_fence_;
  }

  [[nodiscard]] std::uint32_t TypeTag() const override { return kTypeTag; }

 private:
  ::android::sp<::android::GraphicBuffer> buffer_;
  ::android::sp<::android::Fence> ready_fence_;
  // Keeps a GpuSnapshotPool slot unavailable until the serial vendor submit
  // has returned. Normal M3 capture conversions leave this null.
  std::shared_ptr<void> ownership_lease_;
};

// The Android-side half of the presentation adapter. It is the CaptureSink
// called by RenderSurface on SurfaceFlinger's composition thread, but all
// fence waiting, CPU mapping, conversion, and engine submission happens on
// its two worker stages:
//
//   RenderSurface -> one pending CapturedComposition -> conversion worker
//                 -> PresentationAdapter (one pending Frame) -> Engine
//
// Both queues deliberately coalesce to their newest pending value. This is a
// display path, not an every-frame archival path: displaying a stale frame is
// worse than dropping it while the e-ink engine is occupied.
class AndroidEinkPipeline final : public CaptureSink {
 public:
  struct Diagnostics {
    std::uint64_t captures_accepted = 0;
    std::uint64_t captures_coalesced = 0;
    std::uint64_t capture_interval_samples = 0;
    std::uint64_t capture_interval_total_ns = 0;
    std::uint64_t latest_capture_interval_ns = 0;
    std::uint64_t max_capture_interval_ns = 0;
    std::uint64_t conversion_successes = 0;
    std::uint64_t conversion_failures = 0;
    std::uint64_t conversion_duration_samples = 0;
    std::uint64_t conversion_duration_total_ns = 0;
    std::uint64_t conversion_duration_max_ns = 0;
    std::uint64_t source_audit_samples = 0;
    std::uint64_t source_audit_exact_matches = 0;
    std::uint64_t source_audit_mismatches = 0;
    std::uint64_t source_audit_incompatible = 0;
    std::uint64_t source_audit_unavailable = 0;
    std::uint64_t source_direct_selected = 0;
    std::uint64_t latest_source_audit_differing_pixels = 0;
    int latest_source_audit_left = 0;
    int latest_source_audit_top = 0;
    int latest_source_audit_right = 0;
    int latest_source_audit_bottom = 0;
    std::uint64_t frames_enqueued = 0;
    std::uint64_t frames_enqueue_rejected = 0;
    std::uint64_t frames_suppressed_unchanged = 0;
    std::uint64_t frames_selected_session_start = 0;
    std::uint64_t frames_selected_rearm = 0;
    std::uint64_t frames_selected_content_change = 0;
    std::uint64_t normal_differential_selected = 0;
    std::uint64_t normal_differential_accepted = 0;
    std::uint64_t normal_differential_rejected = 0;
    std::uint64_t delta_samples = 0;
    std::uint64_t delta_unavailable = 0;
    std::uint64_t delta_no_change = 0;
    std::uint64_t delta_under_one_percent = 0;
    std::uint64_t delta_under_five_percent = 0;
    std::uint64_t delta_under_twenty_five_percent = 0;
    std::uint64_t delta_twenty_five_percent_or_more = 0;
    std::uint64_t latest_delta_changed_pixels = 0;
    std::uint64_t latest_delta_total_pixels = 0;
    std::uint32_t latest_delta_coverage_ppm = 0;
    int latest_delta_left = 0;
    int latest_delta_top = 0;
    int latest_delta_right = 0;
    int latest_delta_bottom = 0;
    std::uint64_t latest_grayscale_hash = 0;
    std::uint64_t latest_grayscale_bytes = 0;
    std::uint64_t latest_convert_ns = 0;
    std::uint64_t latest_capture_to_convert_ns = 0;
    std::uint64_t latest_capture_to_enqueue_ns = 0;
    std::uint64_t submit_acceptances = 0;
    std::uint64_t submit_rejections = 0;
    std::uint64_t latest_engine_submit_ns = 0;
    std::uint64_t latest_enqueue_to_submit_ns = 0;
    std::uint64_t latest_capture_to_submit_ns = 0;
    std::uint64_t terminal_black_candidates = 0;
    std::uint64_t sleep_image_replacements = 0;
    std::uint64_t sleep_image_suppressions = 0;
    // Legacy diagnostics field name retained for periodic-format compatibility.
    // It now counts black pass-through fallbacks, never last-real-frame holds.
    std::uint64_t terminal_black_last_real_holds = 0;
    std::uint64_t terminal_black_duplicate_suppressions = 0;
    std::uint64_t terminal_black_rearm_events = 0;
    std::uint64_t terminal_black_catalog_ready = 0;
    std::uint64_t terminal_black_catalog_missing = 0;
    std::uint64_t sleep_cycle_arm_accepted = 0;
    std::uint64_t sleep_cycle_arm_rejected = 0;
    std::uint64_t sleep_cycle_disarm_accepted = 0;
    std::uint64_t sleep_cycle_disarm_rejected = 0;
    std::uint64_t sleep_image_present_accepted = 0;
    std::uint64_t sleep_image_present_rejected = 0;
    std::uint64_t sleep_image_capture_holds = 0;
    std::uint64_t sleep_overlay_background_retained = 0;
    std::uint64_t sleep_overlay_background_rejected = 0;
    std::uint64_t sleep_overlay_composited = 0;
    std::uint64_t sleep_overlay_no_background = 0;
    std::uint64_t sleep_overlay_invalid = 0;
    std::uint64_t sleep_overlay_background_released = 0;
    std::uint64_t sleep_cycle_active_epoch = 0;
    std::uint64_t sleep_cycle_last_conversion_sequence = 0;
    std::uint8_t sleep_cycle_latch_state_before_decision = 0;
    std::uint8_t sleep_cycle_last_decision = 0;
    std::uint8_t sleep_cycle_last_enqueue_result = 0;
    std::uint8_t sleep_image_mode = 1;
  };

  struct CaptureAdmissionState {
    bool retained_candidate = false;
    bool conversion_in_flight = false;
    bool pending_capture = false;
    bool adapter_retained = false;
    std::optional<std::chrono::steady_clock::time_point> next_eligible;
  };

  // Every non-identical composed frame uses this same-build engine record.
  // The direct Neo 2 backend admits its normal full-input differential route;
  // the portable pipeline never supplies a caller partial rectangle.
  AndroidEinkPipeline(Engine& engine, Rotation rotation, int engine_mode,
                      int policy_flag = 0,
                      PresentationAdapter::SubmissionIntervalProvider interval_provider = {});
  ~AndroidEinkPipeline() override;

  AndroidEinkPipeline(const AndroidEinkPipeline&) = delete;
  AndroidEinkPipeline& operator=(const AndroidEinkPipeline&) = delete;

  void Start();
  void Stop();

  // Enables or drops new RenderSurface captures. Disabling does not interrupt
  // an already submitted panel update.
  void SetEnabled(bool enabled);

  // Drops any pending capture and resets every policy/baseline that could
  // compare a direct-source frame with a prior snapshot-source frame. The
  // caller must discard the transition capture itself.
  void ResetForSourceChange();

  // This method is called by the private endpoint, never by the composition
  // thread. The framework refreshes a validated selected catalog while awake;
  // the pipeline retains only copied panel-gray bytes for the data-plane latch.
  [[nodiscard]] EinkSleepImageCatalog::PublishResult PublishSleepImageCatalog(
          std::uint64_t epoch, int width, int height,
          std::vector<EinkSleepImageCatalog::Entry> entries, std::size_t selected_index,
          SleepImagePresentationMode mode);
  // These carry only an epoch already accepted by PublishSleepImageCatalog.
  // They cannot fetch, publish, select, or replace catalog data.
  [[nodiscard]] SleepImageArmResult ArmSleepCycle(std::uint64_t epoch);
  [[nodiscard]] SleepImagePresentationResult PresentSleepImage(std::uint64_t epoch);
  [[nodiscard]] SleepImageDisarmResult DisarmSleepCycle(std::uint64_t epoch);

  // CaptureSink implementation. This performs only reference/fence handoff
  // and a short mutex acquisition; it never waits on the composition thread.
  void OnCapturedComposition(CapturedComposition composition) override;

  // Bounded counters for a capture-only integration. They retain no pixel or
  // buffer data and are safe to read from SurfaceFlinger's composition thread.
  [[nodiscard]] Diagnostics diagnostics() const;
  [[nodiscard]] PresentationAdapter::Diagnostics adapter_diagnostics() const;
  // Queue-acceptance scheduling state for RenderSurface's B001 capture
  // admission policy. This is not a completion signal.
  [[nodiscard]] CaptureAdmissionState capture_admission_state() const;

 private:
  void ConversionWorkerMain();
  void OnEngineSubmission(const Frame& frame, bool accepted,
                          std::chrono::steady_clock::duration submit_duration);

  Engine& engine_;
  Rotation rotation_;
  int engine_mode_;
  int policy_flag_;
  PresentationAdapter adapter_;
  AndroidGrayscaleConverter converter_;
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace neo2::eink::android
