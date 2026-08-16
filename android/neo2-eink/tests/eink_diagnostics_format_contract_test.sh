#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || ! -r "$1" ]]; then
  echo "usage: $0 RENDER_SURFACE_CAPTURE_SESSION_CPP" >&2
  exit 2
fi

source_file=$1
adapter_root=$(CDPATH= cd -- "$(dirname -- "$source_file")/../.." && pwd)
pipeline="$adapter_root/android/src/android_eink_pipeline.cpp"
if ! awk '
  index($0, "terminal_black=%llu/%llu/%llu/%llu/%llu/%llu/%llu \"") {
    expect_suppressions = 1
    next
  }
  expect_suppressions && index($0, "\"terminal_black_suppressions=%llu ") {
    found = 1
    exit
  }
  { expect_suppressions = 0 }
  END { exit !found }
' "$source_file"; then
  echo "terminal-black diagnostics must expose seven policy fields followed by suppression" >&2
  exit 1
fi

for counter in \
  terminal_black_candidates \
  sleep_image_replacements \
  terminal_black_last_real_holds \
  terminal_black_duplicate_suppressions \
  terminal_black_rearm_events \
  terminal_black_catalog_ready \
  terminal_black_catalog_missing \
  sleep_image_suppressions; do
  if ! grep -Fq "static_cast<unsigned long long>(diagnostics.$counter)" "$source_file"; then
    echo "missing terminal-black aggregate counter: $counter" >&2
    exit 1
  fi
done

if grep -Eq 'sleep_image_current_epoch|sleep_image_edge_delivery|sleep_image_capture=' "$source_file"; then
  echo "screen-edge diagnostics must not remain authoritative" >&2
  exit 1
fi

grep -Fq 'debug.neo2.eink.sleep_trace' "$source_file"
grep -Fq 'FreshDiagnosticsSnapshot' "$source_file"
grep -Fq 'GetNeo2EinkFreshDiagnosticsSnapshot' "$source_file"
grep -Fq 'neo2_eink_diagnostics_fresh disabled' "$source_file"
for field in catalog_epoch arm= disarm= active_epoch latch_before decision conversion_sequence enqueue= lower_submission lower_result; do
  grep -Fq "$field" "$source_file"
done
if awk '
  {
    line = tolower($0)
    if (line ~ /(pixel_bytes|image_hash|descriptor|caller_identity|raw_frame|path=)/ &&
        line ~ /(sleep_cycle|diagnostics_fresh)/) {
      found = 1
      exit
    }
  }
  END { exit !found }
' "$source_file"; then
  echo "sleep-cycle diagnostics expose prohibited data" >&2
  exit 1
fi

awk '
  index($0, "if (SleepCycleTraceEnabled() && sleep_evaluation.valid_full_frame) {") {
    expect_trace = 1
    next
  }
  expect_trace && index($0, "ALOGI(\"sleep_frame_evaluated sequence=%llu min=%u max=%u mean=%u threshold=%u \"") {
    found = 1
    exit
  }
  { expect_trace = 0 }
  END { exit !found }
' "$pipeline"
awk '
  index($0, "if (SleepCycleTraceEnabled()) {") {
    expect_trace = 1
    next
  }
  expect_trace && index($0, "ALOGI(\"sleep_frame_lower sequence=%llu result=%s\"") {
    found = 1
    exit
  }
  { expect_trace = 0 }
  END { exit !found }
' "$pipeline"
for field in sequence=%llu min=%u max=%u mean=%u threshold=%u ready=%d holding=%d terminal_decision=%d; do
  grep -Fq "$field" "$pipeline"
done
if grep -Eiq 'sleep_frame_(evaluated|lower).*pixel|sleep_frame_(evaluated|lower).*hash|sleep_frame_(evaluated|lower).*path|sleep_frame_(evaluated|lower).*image|sleep_frame_(evaluated|lower).*completion' "$pipeline"; then
  echo "sleep-frame trace exposes prohibited data or a completion claim" >&2
  exit 1
fi

echo "PASS"
