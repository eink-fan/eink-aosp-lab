#!/usr/bin/env bash
set -euo pipefail

adapter_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
frame_header="$adapter_root/include/eink_presentation_adapter.h"
adapter="$adapter_root/src/eink_presentation_adapter.cpp"
latch="$adapter_root/src/eink_sleep_image_latch.cpp"
pipeline="$adapter_root/android/src/android_eink_pipeline.cpp"
session="$adapter_root/android/src/render_surface_capture_session.cpp"
binder="$adapter_root/android/src/sleep_image_binder_service.cpp"

grep -Fq 'std::uint64_t sleep_image_epoch = 0;' "$frame_header"
grep -Fq 'CancelPendingSleepImage(std::uint64_t epoch)' "$adapter"
grep -Fq 'SleepImagePresentationResult SleepImageLatch::BeginPresentation' "$latch"
grep -Fq 'SleepImagePresentationResult AndroidEinkPipeline::PresentSleepImage' "$pipeline"
grep -Fq 'impl_->sleep_image_catalog->CandidateAt' "$pipeline"
grep -Fq '++impl_->source_generation;' "$pipeline"
grep -Fq 'impl_->pending.reset();' "$pipeline"
grep -Fq '.sleep_image_epoch = epoch' "$pipeline"
grep -Fq 'adapter_.Enqueue(std::move(frame))' "$pipeline"
grep -Fq 'adapter_.CancelPendingSleepImage(epoch)' "$pipeline"
grep -Fq 'sleep_image_latch.state() == SleepImageLatchState::kConsumed' "$pipeline"
grep -Fq 'pipeline->PresentSleepImage(epoch)' "$session"
grep -Fq 'g_session->PresentSleepImage(epoch)' "$binder"
! grep -Eq 'sleep_for|postDelayed|maximum_gray.*PresentSleepImage' "$pipeline"

printf 'Sleep-image direct-presentation contract tests passed.\n'
