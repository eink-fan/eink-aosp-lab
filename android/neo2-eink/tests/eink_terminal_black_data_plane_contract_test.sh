#!/usr/bin/env bash
set -euo pipefail

adapter_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pipeline="$adapter_root/android/src/android_eink_pipeline.cpp"
latch="$adapter_root/src/eink_sleep_image_latch.cpp"
latch_header="$adapter_root/include/eink_sleep_image_latch.h"
session="$adapter_root/android/src/render_surface_capture_session.cpp"
lower_backend="$adapter_root/android/src/lower_engine_direct_backend.cpp"

grep -Fq 'inline constexpr std::uint8_t kSleepImageMaximumGray = 96;' "$latch_header"
grep -Fq 'frame.grayscale_buffer->ByteCount() != frame_size' "$latch"
grep -Fq 'last_evaluation_.maximum_gray > kSleepImageMaximumGray' "$latch"
for field in minimum_gray maximum_gray mean_gray threshold ready holding decision outcome; do
  grep -Fq "$field" "$latch_header"
done
grep -Fq 'TerminalBlackOutcome::kCatalogMissingFallback' "$latch"
grep -Fq 'TerminalBlackOutcome::kDuplicateSuppressed' "$latch"
! grep -Fq 'TerminalBlackOutcome::kRearmed' "$latch"
grep -Fq 'SleepImageArmResult SleepImageLatch::ArmSleepCycle' "$latch"
grep -Fq 'SleepImageDisarmResult SleepImageLatch::DisarmSleepCycle' "$latch"
grep -Fq 'SleepImageLatchState state_ = SleepImageLatchState::kInactive;' "$latch_header"
grep -Fq 'state_ = SleepImageLatchState::kArmed;' "$latch"
grep -Fq 'state_ = SleepImageLatchState::kInactive;' "$latch"
grep -Fq 'if (state_ == SleepImageLatchState::kInactive)' "$latch"
grep -Fq 'impl_->sleep_image_latch.Evaluate(' "$pipeline"
grep -Fq 'RecordTerminalBlackDiagnostics' "$pipeline"
grep -Fq 'sleep_frame_evaluated sequence=%llu min=%u max=%u mean=%u threshold=%u ' "$pipeline"
grep -Fq 'ready=%d holding=%d terminal_decision=%d' "$pipeline"
grep -Fq 'sleep_frame_lower sequence=%llu result=%s' "$pipeline"
decision_line=$(awk '/sleep_image_latch\.Evaluate/ { print NR; exit }' "$pipeline")
enqueue_line=$(awk -v decision_line="$decision_line" \
  'NR > decision_line && /adapter_\.Enqueue/ { print NR; exit }' "$pipeline")
if [[ -z "$decision_line" || -z "$enqueue_line" || "$decision_line" -ge "$enqueue_line" ]]; then
  echo "terminal-black decision must precede its enqueue" >&2
  exit 1
fi
grep -Fq 'if (catalog)' "$pipeline"
! grep -Fq 'sleep_image_catalog_epoch == impl_->sleep_image_latch.active_epoch()' "$pipeline"
grep -Fq 'pipeline->ArmSleepCycle(*pending_arm)' "$session"
! grep -Eq 'SetScreen(Off|On)Epoch|force_sleep_capture|pending_sleep_capture' "$pipeline" "$session"

grep -Fq 'constexpr int kM4DefaultMinimumIntervalMs = 50;' "$lower_backend"
grep -Fq 'constexpr int kM4MinimumIntervalMs = 50;' "$lower_backend"
grep -Fq 'static_assert(NormalizeMinimumIntervalMs(kM4DefaultMinimumIntervalMs) == 50);' "$lower_backend"
grep -Fq 'static_assert(NormalizeMinimumIntervalMs(49) == 50);' "$lower_backend"
grep -Fq 'static_assert(NormalizeMinimumIntervalMs(250) == 250);' "$lower_backend"
grep -Fq 'GetIntProperty(kM4MinimumIntervalProperty' "$lower_backend"
grep -Fq 'NormalizeMinimumIntervalMs(configured)' "$lower_backend"

echo "PASS"
