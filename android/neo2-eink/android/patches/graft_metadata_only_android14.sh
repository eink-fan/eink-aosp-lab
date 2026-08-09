#!/bin/sh
# Graft the Neo 2 metadata-only RenderSurface hook into a *full*, disposable
# AOSP source checkout. This script never talks to a device or builds/flashes
# anything. It deliberately refuses a source revision other than the pinned
# Android 14 frameworks/native checkout.

set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 /path/to/aosp-root" >&2
  exit 64
fi

AOSP_ROOT=$1
NATIVE_ROOT=$AOSP_ROOT/frameworks/native
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ADAPTER_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
PATCH_FILE=$SCRIPT_DIR/android14-metadata-only.patch
TARGET_DIR=$NATIVE_ROOT/services/surfaceflinger/Neo2Eink
PINNED_REVISION=bfcf75076e562945bae131a4929f29a90d0d2481

if [ ! -e "$NATIVE_ROOT/.git" ]; then
  echo "error: $NATIVE_ROOT is not a git checkout" >&2
  exit 1
fi
if [ "$(git -C "$NATIVE_ROOT" rev-parse HEAD)" != "$PINNED_REVISION" ]; then
  echo "error: frameworks/native is not the pinned Android 14 revision" >&2
  exit 1
fi
if [ -n "$(git -C "$NATIVE_ROOT" status --porcelain)" ]; then
  echo "error: frameworks/native has local changes; use a clean disposable tree" >&2
  exit 1
fi
if [ -e "$TARGET_DIR" ]; then
  echo "error: target already exists: $TARGET_DIR" >&2
  exit 1
fi

git -C "$NATIVE_ROOT" apply --check "$PATCH_FILE"
cp -R "$ADAPTER_ROOT" "$TARGET_DIR"
git -C "$NATIVE_ROOT" apply "$PATCH_FILE"

echo "Applied metadata-only Neo 2 capture hook to $NATIVE_ROOT"
echo "Review with: git -C $NATIVE_ROOT diff --check && git -C $NATIVE_ROOT diff"
echo "This is not a pixel-snapshot or vendor-engine integration."
