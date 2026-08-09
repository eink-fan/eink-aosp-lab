#!/bin/sh
# Upgrade an already M1-grafted disposable Android 14 frameworks/native tree
# to the owned-snapshot / CaptureOnlyEngine M3 experiment. This script does
# not build, flash, communicate with the reader, or enable vendor output.

set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 /path/to/aosp-root" >&2
  exit 64
fi

AOSP_ROOT=$1
NATIVE_ROOT=$AOSP_ROOT/frameworks/native
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ADAPTER_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
PATCH_FILE=$SCRIPT_DIR/android14-m3-capture-only.patch
TARGET_DIR=$NATIVE_ROOT/services/surfaceflinger/Neo2Eink
CE_BP=$NATIVE_ROOT/services/surfaceflinger/CompositionEngine/Android.bp

if [ ! -d "$TARGET_DIR" ]; then
  echo "error: M1 adapter directory is absent: $TARGET_DIR" >&2
  exit 1
fi
if ! grep -q -- '-DNEO2_EINK_METADATA_ONLY=1' "$CE_BP"; then
  echo "error: M1 metadata-only CompositionEngine graft is absent" >&2
  exit 1
fi
if grep -q -- '-DNEO2_EINK_M3_CAPTURE_ONLY=1' "$CE_BP"; then
  echo "error: M3 capture-only patch is already active" >&2
  exit 1
fi

patch --batch --dry-run -d "$NATIVE_ROOT" -p1 < "$PATCH_FILE"
cp -R "$ADAPTER_ROOT"/. "$TARGET_DIR"
patch --batch -d "$NATIVE_ROOT" -p1 < "$PATCH_FILE"

echo "Applied M3 owned-snapshot / CaptureOnlyEngine activation to $NATIVE_ROOT"
echo "Review with: grep -R -n NEO2_EINK_M3_CAPTURE_ONLY $NATIVE_ROOT/services/surfaceflinger"
echo "M3 copies only into fixed owned buffers; it has no vendor engine, DRM, or panel-output call."