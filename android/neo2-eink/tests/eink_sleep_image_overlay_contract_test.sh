#!/usr/bin/env bash
set -euo pipefail

adapter_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
catalog_header="$adapter_root/android/include/neo2/eink/eink_sleep_image_catalog.h"
pipeline="$adapter_root/android/src/android_eink_pipeline.cpp"
binder="$adapter_root/android/src/sleep_image_binder_service.cpp"
session="$adapter_root/android/src/render_surface_capture_session.cpp"
provider="$adapter_root/android/frontlight-app/src/org/neo2/controls/SleepImageCatalogService.java"
transforms="$adapter_root/android/frontlight-app/src/org/neo2/controls/SleepImageTransforms.java"
controls="$adapter_root/android/frontlight-app/src/org/neo2/controls/ControlsActivity.java"
android_bp="$adapter_root/Android.bp"

grep -Fq 'std::vector<std::uint8_t> alpha;' "$catalog_header"
grep -Fq 'kMaximumEntries * kPanelBytes * 2' "$catalog_header"
grep -Fq 'constexpr std::int32_t kProtocolVersion = 2;' "$binder"
grep -Fq 'ReadPlane(data, &entry->panel_gray) && ReadPlane(data, &entry->alpha)' "$binder"
grep -Fq 'SleepImagePresentationMode::kOverlay' "$binder"
grep -Fq 'sleep_image_background.PrepareOverlay' "$pipeline"
grep -Fq 'sleep_image_background.ObserveSubmission(frame, accepted)' "$pipeline"
grep -Fq 'sleep_image_background.Release()' "$pipeline"
grep -Fq '.sleep_image_epoch = sleep_decision == SleepImageDecision::kReplace' "$pipeline"
grep -Fq 'pending_catalog->mode' "$session"
grep -Fq 'java.util.Arrays.fill(gray, (byte) 0xf0)' "$transforms"
grep -Fq 'alpha[destination] = (byte) (argb >>> 24);' "$transforms"
grep -Fq 'data.writeFileDescriptor(alphaEntry.getFileDescriptor())' "$provider"
grep -Fq 'MODE_OVERLAY_SLEEP_IMAGE = 2' "$controls"
grep -Fq '"src/eink_sleep_image_background.cpp"' "$android_bp"
grep -Fq '"src/eink_sleep_image_overlay.cpp"' "$android_bp"

printf 'Sleep-image overlay integration contract tests passed.\n'
