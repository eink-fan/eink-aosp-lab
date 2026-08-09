#!/bin/sh
# Upgrade a disposable M3-grafted Android 14 tree to the explicit M4 direct
# lower-engine one-shot candidate. This script only changes source. It does
# not build, install, boot, dlopen, initialize an engine, or touch the reader.

set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 /path/to/aosp-root" >&2
  exit 64
fi

AOSP_ROOT=$1
NATIVE_ROOT=$AOSP_ROOT/frameworks/native
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ADAPTER_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
PATCH_FILE=$SCRIPT_DIR/android14-m4-lower-engine-one-shot.patch
TARGET_DIR=$NATIVE_ROOT/services/surfaceflinger/Neo2Eink
CE_BP=$NATIVE_ROOT/services/surfaceflinger/CompositionEngine/Android.bp

if [ ! -d "$TARGET_DIR" ]; then
  echo "error: M1 adapter directory is absent: $TARGET_DIR" >&2
  exit 1
fi
if ! grep -q -- '-DNEO2_EINK_M3_CAPTURE_ONLY=1' "$CE_BP"; then
  echo "error: M3 capture-only graft is absent" >&2
  exit 1
fi
if grep -q -- '-DNEO2_EINK_M4_LOWER_ENGINE_OUTPUT=1' "$CE_BP"; then
  echo "error: M4 direct lower-engine output patch is already active" >&2
  exit 1
fi

patch --batch --dry-run -d "$NATIVE_ROOT" -p1 < "$PATCH_FILE"
# The normal invocation runs this script from the workspace adapter and copies
# it into the disposable AOSP tree. Keeping the identity guard makes a remote
# source-only compile of an already-synced adapter deterministic too.
if [ "$ADAPTER_ROOT" != "$TARGET_DIR" ]; then
  cp -R "$ADAPTER_ROOT"/. "$TARGET_DIR"
fi
patch --batch -d "$NATIVE_ROOT" -p1 < "$PATCH_FILE"

echo "Applied M4 lower-engine one-shot source graft to $NATIVE_ROOT"
echo "This selects exactly one direct raw-gray update per SurfaceFlinger process."
echo "It remains a source-only candidate until separately built, reviewed, and approved."
