#!/usr/bin/env bash
set -euo pipefail

adapter_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pipeline="$adapter_root/android/src/android_eink_pipeline.cpp"
session="$adapter_root/android/src/render_surface_capture_session.cpp"
binder="$adapter_root/android/src/sleep_image_binder_service.cpp"

grep -Fq 'RegisterSleepImageCaptureSession(this)' "$session"
grep -Fq 'PublishSleepImageCatalog(' "$session"
grep -Fq 'ArmSleepCycle(std::uint64_t epoch)' "$session"
grep -Fq 'DisarmSleepCycle(std::uint64_t epoch)' "$session"
grep -Fq 'pending_sleep_cycle_arm' "$session"
grep -Fq 'pipeline->ArmSleepCycle(*pending_arm)' "$session"

grep -Fq 'sleep_image_latch.Evaluate' "$pipeline"
grep -Fq 'sleep_image_background.Release()' "$pipeline"
grep -Fq 'CancelPendingSleepImage(epoch)' "$pipeline"
grep -Fq 'PublishSleepImageBinderService()' "$binder"

if grep -Eq 'ACTION_SCREEN_(ON|OFF)|force_sleep_capture|pending_sleep_capture' \
    "$pipeline" "$session" "$binder"; then
  echo 'sleep-image lifecycle must not be inferred from broadcasts or forced capture' >&2
  exit 1
fi

printf 'Sleep-image lifecycle source contract passed.\n'
