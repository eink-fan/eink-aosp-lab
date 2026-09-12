#!/usr/bin/env bash
set -euo pipefail
app_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
classes=$(mktemp -d "${TMPDIR:-/tmp}/ink-controls-java.XXXXXX")
trap 'rm -rf "$classes"' EXIT
source_dir="$app_root/src/org/neo2/controls"
javac -d "$classes" \
  "$source_dir/EinkDeviceProfile.java" \
  "$source_dir/SleepImageStore.java" \
  "$source_dir/SleepImageTransforms.java" \
  "$source_dir/GrayPreferenceSelection.java" \
  "$source_dir/panel/SwitchPolicy.java" \
  "$source_dir/panel/DisplayCommand.java" \
  "$app_root/tests/GrayPreferenceSelectionHostTest.java" \
  "$app_root/tests/SleepImageCatalogHostTest.java" \
  "$app_root/tests/SwitchPolicyTest.java" \
  "$app_root/tests/DisplayCommandTest.java"
java -cp "$classes" org.neo2.controls.GrayPreferenceSelectionHostTest
java -cp "$classes" org.neo2.controls.SleepImageCatalogHostTest
java -cp "$classes" org.neo2.controls.panel.SwitchPolicyTest
java -cp "$classes" org.neo2.controls.panel.DisplayCommandTest
