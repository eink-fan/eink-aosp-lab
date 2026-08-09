#!/bin/sh
# Stage (but do not install or boot) the exact stock 64-bit e-ink runtime
# family into a disposable Android 14 source tree for the M4 linker probe.
# This script never builds, flashes, communicates with the reader, or adds a
# live SurfaceFlinger hook.

set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: $0 /path/to/aosp-root /path/to/stock-eink-analysis-input" >&2
  exit 64
fi

AOSP_ROOT=$1
INPUT_ROOT=$2
NATIVE_ROOT=$AOSP_ROOT/frameworks/native
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ADAPTER_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TARGET_ROOT=$NATIVE_ROOT/services/surfaceflinger/Neo2Eink/android
RUNTIME_ROOT=$TARGET_ROOT/vendor-runtime
TARGET_BP=$TARGET_ROOT/Android.bp
FAMILY_ROOT=$INPUT_ROOT/vendor-runtime-family
CLOSURE_ROOT=$INPUT_ROOT/vendor-runtime-closure
WAVEFORM_ROOT=$INPUT_ROOT/waveforms
FRONTLIGHT_ROOT=$INPUT_ROOT/frontlight

if [ ! -f "$TARGET_BP" ]; then
  echo "error: Neo2Eink Android source graft is absent: $TARGET_BP" >&2
  exit 1
fi
for library in libeinksfpatch.so libidisplayengine.so libidisplayV3.so libeinkutils.so; do
  test -f "$INPUT_ROOT/$library" || {
    echo "error: missing retained stock artifact: $library" >&2
    exit 1
  }
done
test -f "$FAMILY_ROOT/SHA256SUMS" || {
  echo "error: vendor runtime checksum manifest is absent" >&2
  exit 1
}
test -f "$CLOSURE_ROOT/SHA256SUMS" || {
  echo "error: closure checksum manifest is absent" >&2
  exit 1
}
test -f "$WAVEFORM_ROOT/SHA256SUMS" || {
  echo "error: waveform checksum manifest is absent" >&2
  exit 1
}
test -f "$FRONTLIGHT_ROOT/neo2_frontlight_calibration.conf" || {
  echo "error: front-light calibration payload is absent" >&2
  exit 1
}
test -f "$FRONTLIGHT_ROOT/SHA256SUMS" || {
  echo "error: front-light calibration checksum manifest is absent" >&2
  exit 1
}
for waveform in default_wbf.bin regal.wbf default_nm.bin; do
  test -f "$WAVEFORM_ROOT/$waveform" || {
    echo "error: missing retained waveform resource: $waveform" >&2
    exit 1
  }
done
(
  cd "$WAVEFORM_ROOT"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum -c SHA256SUMS
  else
    shasum -a 256 -c SHA256SUMS
  fi
)
(
  cd "$FRONTLIGHT_ROOT"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum -c SHA256SUMS
  else
    shasum -a 256 -c SHA256SUMS
  fi
)

mkdir -p "$RUNTIME_ROOT/payload-system" "$RUNTIME_ROOT/payload-system_ext" \
  "$RUNTIME_ROOT/waveforms" "$RUNTIME_ROOT/frontlight"
cp "$INPUT_ROOT"/libeinksfpatch.so "$RUNTIME_ROOT/"
cp "$INPUT_ROOT"/libidisplayengine.so "$RUNTIME_ROOT/"
cp "$INPUT_ROOT"/libidisplayV3.so "$RUNTIME_ROOT/"
cp "$INPUT_ROOT"/libeinkutils.so "$RUNTIME_ROOT/"
cp "$FAMILY_ROOT"/*.so "$RUNTIME_ROOT/"
cp "$FAMILY_ROOT/SHA256SUMS" "$RUNTIME_ROOT/eink-family-SHA256SUMS"
cp "$CLOSURE_ROOT/system/lib64"/*.so "$RUNTIME_ROOT/payload-system/"
cp "$CLOSURE_ROOT/system_ext/lib64"/*.so "$RUNTIME_ROOT/payload-system_ext/"
cp "$CLOSURE_ROOT/SHA256SUMS" "$RUNTIME_ROOT/closure-SHA256SUMS"
cp "$WAVEFORM_ROOT/default_wbf.bin" "$RUNTIME_ROOT/waveforms/"
cp "$WAVEFORM_ROOT/regal.wbf" "$RUNTIME_ROOT/waveforms/"
cp "$WAVEFORM_ROOT/default_nm.bin" "$RUNTIME_ROOT/waveforms/"
cp "$WAVEFORM_ROOT/SHA256SUMS" "$RUNTIME_ROOT/waveforms-SHA256SUMS"
cp "$FRONTLIGHT_ROOT/neo2_frontlight_calibration.conf" "$RUNTIME_ROOT/frontlight/"
cp "$FRONTLIGHT_ROOT/SHA256SUMS" "$RUNTIME_ROOT/frontlight-SHA256SUMS"
cp "$ADAPTER_ROOT"/android/src/vendor_sync_link_probe.cpp "$RUNTIME_ROOT/"
cp "$SCRIPT_DIR/m4_vendor_runtime_payload.mk" "$RUNTIME_ROOT/"

# The source-graft Android.bp owns all modules in this directory. Replace only
# the generated tail so reruns are deterministic and never duplicate Soong
# module names. The marker also matches the earlier link-only template.
STAGED_BP=$(mktemp "$TARGET_ROOT/.Android.bp.m4.XXXXXX")
awk '
  /^\/\/ Generated into Neo2Eink\/.*vendor-runtime/ { exit }
  { print }
' "$TARGET_BP" > "$STAGED_BP"
cat "$SCRIPT_DIR/vendor-runtime-Android.bp" >> "$STAGED_BP"
mv "$STAGED_BP" "$TARGET_BP"

echo "Staged M4 runtime payload inputs at $RUNTIME_ROOT"
echo "The product include remains off unless NEO2_M4_RUNTIME_PAYLOAD=true."
echo "This script neither builds, installs, boots, dlopens, nor selects a vendor SurfaceFlinger path."
