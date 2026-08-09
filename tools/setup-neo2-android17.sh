#!/usr/bin/env bash
# Create a local Android-17 checkout and stage an already extracted payload.
# It never contacts, installs to, or otherwise changes a device.
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
profile="$repo_root/profiles/neo2-android17"
workspace=""
payload=""
jobs=4
usage() { printf '%s\n' "usage: $0 --workspace NEW_DIRECTORY --payload LOCAL_PAYLOAD [--jobs N]"; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --workspace) workspace=${2:?missing workspace}; shift 2 ;;
    --payload) payload=${2:?missing payload}; shift 2 ;;
    --jobs) jobs=${2:?missing jobs}; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 64 ;;
  esac
done
[[ -n "$workspace" && -d "$payload" && "$jobs" =~ ^[1-9][0-9]*$ ]] || { usage >&2; exit 64; }
[[ ! -e "$workspace" || -z $(find "$workspace" -mindepth 1 -maxdepth 1 -print -quit) ]] || { printf '%s\n' 'error: workspace must be new or empty' >&2; exit 1; }
command -v repo >/dev/null || { printf '%s\n' 'error: Android repo tool is unavailable' >&2; exit 1; }
command -v git >/dev/null || { printf '%s\n' 'error: git is unavailable' >&2; exit 1; }
command -v sha256sum >/dev/null || { printf '%s\n' 'error: sha256sum is unavailable' >&2; exit 1; }
command -v python3 >/dev/null || { printf '%s\n' 'error: python3 is unavailable' >&2; exit 1; }
[[ $(uname -s) == Linux ]] || { printf '%s\n' 'error: this AOSP workflow requires a Linux build host' >&2; exit 1; }
mkdir -p "$workspace"
workspace=$(CDPATH= cd -- "$workspace" && pwd)
payload=$(CDPATH= cd -- "$payload" && pwd)

[[ -f "$payload/SOURCE_BUILD.txt" && -f "$payload/SHA256SUMS" ]] || {
  printf '%s\n' 'error: payload lacks SOURCE_BUILD.txt or SHA256SUMS; rerun the checked-in extractor' >&2
  exit 1
}
(
  cd "$payload"
  sha256sum --check --strict SHA256SUMS >/dev/null
) || { printf '%s\n' 'error: payload SHA256SUMS verification failed' >&2; exit 1; }

source_value() { awk -F= -v key="$1" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$payload/SOURCE_BUILD.txt"; }
expected_source_value() { awk -F= -v key="$1" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$profile/payload-source.env"; }
[[ $(source_value schema) == neo2-payload-source-v1 ]] || {
  printf '%s\n' 'error: unrecognized payload source record; rerun the checked-in extractor' >&2
  exit 1
}
for key in ro.product.device ro.build.version.release; do
  actual=$(source_value "$key")
  expected=$(expected_source_value "$key")
  [[ -n "$actual" && "$actual" == "$expected" ]] || {
    printf 'error: payload %s is %q; profile requires %q\n' "$key" "$actual" "$expected" >&2
    exit 1
  }
done
[[ -n $(source_value ro.build.fingerprint) ]] || {
  printf '%s\n' 'error: payload source record lacks ro.build.fingerprint' >&2
  exit 1
}

while IFS= read -r path || [[ -n "$path" ]]; do
  [[ -z "$path" || "$path" == \#* ]] && continue
  [[ -f "$payload/$path" ]] || { printf 'error: payload file missing: %s\n' "$path" >&2; exit 1; }
  expected=$(awk -v target="$path" '$2 == target { print $1; exit }' "$profile/vendor-files.sha256")
  [[ -n "$expected" ]] || { printf 'error: payload lock has no entry: %s\n' "$path" >&2; exit 1; }
  actual=$(sha256sum "$payload/$path" | awk '{ print $1 }')
  [[ "$actual" == "$expected" ]] || { printf 'error: payload hash does not match profile lock: %s\n' "$path" >&2; exit 1; }
done < "$profile/vendor-files.txt"
[[ $(awk -F= '$1 == "schema" { print $2; exit }' "$payload/frontlight/neo2_frontlight_calibration.conf" 2>/dev/null) == neo2-frontlight-v1 ]] || {
  printf '%s\n' 'error: front-light config is absent or malformed' >&2
  exit 1
}

manifest_url=$(awk -F= '$1 == "manifest_url" {print $2}' "$profile/aosp-manifest-source.env")
manifest_revision=$(awk -F= '$1 == "manifest_revision" {print $2}' "$profile/aosp-manifest-source.env")
manifest_file=$(awk -F= '$1 == "manifest_file" {print $2}' "$profile/aosp-manifest-source.env")
superproject_revision=$(awk -F= '$1 == "superproject_revision" {print $2}' "$profile/aosp-manifest-source.env")
[[ "$superproject_revision" =~ ^[0-9a-f]{40}$ ]] || { printf '%s\n' 'error: invalid superproject revision' >&2; exit 1; }
aosp="$workspace/aosp"
mkdir -p "$aosp"
(
  cd "$aosp"
  repo init -u "$manifest_url" -b "$manifest_revision" -m "$manifest_file" --depth=1
  manifest_path="$aosp/.repo/manifests/$manifest_file"
  python3 - "$manifest_path" "$superproject_revision" <<'PY'
import sys
import xml.etree.ElementTree as ET

path, revision = sys.argv[1:]
tree = ET.parse(path)
entry = tree.getroot().find("superproject")
if entry is None:
    raise SystemExit("error: pinned manifest has no superproject entry")
entry.set("revision", revision)
tree.write(path, encoding="UTF-8", xml_declaration=True)
PY
  repo sync --use-superproject -c --no-tags --no-clone-bundle -j"$jobs"
  repo manifest -r -o "$workspace/aosp-resolved-manifest.xml"
  sha256sum "$workspace/aosp-resolved-manifest.xml" > "$workspace/aosp-resolved-manifest.sha256"
)

while IFS= read -r patch; do
  relative=${patch#"$profile/aosp-patches/"}
  project=${relative%.patch}
  git -C "$aosp/$project" apply --check "$patch"
  git -C "$aosp/$project" apply "$patch"
  git -C "$aosp/$project" diff --check
done < <(find "$profile/aosp-patches" -type f -name '*.patch' -print | LC_ALL=C sort)

graft="$aosp/frameworks/native/services/surfaceflinger/Neo2Eink"
[[ ! -e "$graft" ]] || { printf '%s\n' 'error: Neo2Eink graft already exists' >&2; exit 1; }
mkdir -p "$(dirname "$graft")"
cp -R "$repo_root/android/neo2-eink" "$graft"
runtime="$graft/android/vendor-runtime"
mkdir -p "$runtime"
while IFS= read -r path || [[ -n "$path" ]]; do
  [[ -z "$path" || "$path" == \#* ]] && continue
  mkdir -p "$(dirname "$runtime/$path")"
  cp "$payload/$path" "$runtime/$path"
done < "$profile/vendor-files.txt"
mkdir -p "$runtime/frontlight"
cp "$payload/frontlight/neo2_frontlight_calibration.conf" "$runtime/frontlight/"
cp "$graft/android/patches/m4_vendor_runtime_payload.mk" "$runtime/"
cp "$graft/android/src/vendor_sync_link_probe.cpp" "$runtime/"
staged_bp=$(mktemp "$graft/android/.Android.bp.runtime.XXXXXX")
awk '/^\/\/ Generated into Neo2Eink\/.*vendor-runtime/ { exit } { print }' "$graft/android/Android.bp" > "$staged_bp"
cat "$graft/android/patches/vendor-runtime-Android.bp" >> "$staged_bp"
mv "$staged_bp" "$graft/android/Android.bp"
printf 'Prepared Android-17 workspace: %s\n' "$workspace"
printf 'Build with: %s/tools/build-neo2-android17.sh --workspace %s\n' "$repo_root" "$workspace"
