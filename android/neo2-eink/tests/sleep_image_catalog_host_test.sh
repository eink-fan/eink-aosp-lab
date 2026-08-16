#!/usr/bin/env bash
set -euo pipefail

adapter_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
classes=$(mktemp -d "${TMPDIR:-/tmp}/neo2-sleep-catalog-java.XXXXXX")
trap 'find "$classes" -depth -delete' EXIT

javac -d "$classes" \
  "$adapter_root/android/frontlight-app/src/org/neo2/controls/SleepImageStore.java" \
  "$adapter_root/android/frontlight-app/src/org/neo2/controls/SleepImageTransforms.java" \
  "$adapter_root/tests/java/org/neo2/controls/SleepImageCatalogHostTest.java"
java -cp "$classes" org.neo2.controls.SleepImageCatalogHostTest
