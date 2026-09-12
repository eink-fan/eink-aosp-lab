#!/usr/bin/env bash
# Create a candidate-specific, offline-only AVB/FEC DSU system artifact.
set -euo pipefail

workspace=""
source_image=""
artifact_dir=""
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
profile="$repo_root/profiles/neo2-android17/dsu-profile.env"

usage() {
  cat <<'EOF'
usage: prepare-android17-artifact.sh --workspace DIRECTORY --artifact-dir DIRECTORY [options]

Wrap a built Neo 2 Android 17 system.img in the proven AVB/FEC system
partition format. This is host-only and never contacts a device.

Options:
  --workspace PATH        local workspace created by setup-neo2-android17.sh (required)
  --source-image PATH     built system image (default: WORKSPACE/aosp/out/.../system.img)
  --artifact-dir PATH     new destination directory (required)
  --profile PATH          DSU profile (default: profiles/neo2-android17/dsu-profile.env)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --workspace) workspace=${2:?missing value}; shift 2 ;;
    --source-image) source_image=${2:?missing value}; shift 2 ;;
    --artifact-dir) artifact_dir=${2:?missing value}; shift 2 ;;
    --profile) profile=${2:?missing value}; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) printf 'error: unknown argument: %s\n' "$1" >&2; usage >&2; exit 64 ;;
  esac
done

[[ -n "$workspace" && -n "$artifact_dir" ]] || { printf 'error: --workspace and --artifact-dir are required\n' >&2; exit 64; }
[[ "$workspace" = /* && "$artifact_dir" = /* ]] || {
  printf 'error: workspace and artifact directory must be absolute\n' >&2; exit 64;
}
[[ -f "$profile" ]] || { printf 'error: DSU profile is absent: %s\n' "$profile" >&2; exit 1; }
profile_value() { awk -F= -v key="$1" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$profile"; }
[[ $(profile_value schema) == neo2-android17-dsu-v1 || $(profile_value schema) == eink-dsu-v1 ]] || { printf '%s\n' 'error: unsupported DSU profile' >&2; exit 1; }
aosp_subdirectory=$(profile_value aosp_subdirectory)
system_image_relative=$(profile_value system_image_relative)
artifact_name=$(profile_value artifact_name)
partition_name=$(profile_value partition_name)
partition_size=$(profile_value partition_size)
avbtool_relative=$(profile_value avbtool_relative)
avb_testkey_relative=$(profile_value avb_testkey_relative)
host_tools_relative=$(profile_value host_tools_relative)
avb_algorithm=$(profile_value avb_algorithm)
avb_hash_algorithm=$(profile_value avb_hash_algorithm)
avb_fec_num_roots=$(profile_value avb_fec_num_roots)
avb_rollback_index=$(profile_value avb_rollback_index)
[[ "$aosp_subdirectory" != */* && "$artifact_name" != */ && "$artifact_name" != *"'"* && \
   "$system_image_relative" != /* && "$avbtool_relative" != /* && "$avb_testkey_relative" != /* && "$host_tools_relative" != /* && \
   "$partition_name" =~ ^[A-Za-z0-9_.-]+$ && "$partition_size" =~ ^[1-9][0-9]*$ && \
   "$avb_fec_num_roots" =~ ^[1-9][0-9]*$ && "$avb_rollback_index" =~ ^[0-9]+$ ]] || {
  printf '%s\n' 'error: DSU profile contains invalid values' >&2; exit 1;
}

aosp="$workspace/$aosp_subdirectory"
source_image=${source_image:-"$aosp/$system_image_relative"}
artifact="$artifact_dir/$artifact_name"
avbtool="$aosp/$avbtool_relative"
testkey="$aosp/$avb_testkey_relative"
test -r "$source_image"
test -r "$avbtool"
test -r "$testkey"
test ! -e "$artifact_dir"
artifact_parent=$artifact_dir
while [[ ! -e "$artifact_parent" ]]; do artifact_parent=$(dirname "$artifact_parent"); done
artifact_parent=$(CDPATH= cd -- "$artifact_parent" && pwd -P)
case "$artifact_parent" in
  "$repo_root"|"$repo_root"/*)
    printf '%s\n' 'error: artifact directory must be outside this repository' >&2
    exit 64 ;;
esac
for command in python3 debugfs sha256sum; do
  command -v "$command" >/dev/null || { printf 'error: required command is unavailable: %s\n' "$command" >&2; exit 1; }
done
mkdir -p "$artifact_dir"

prop_file=$(mktemp "$artifact_dir/.build-prop.XXXXXX")
trap 'rm -f "$prop_file" "$artifact_dir/system.img"' EXIT
debugfs -R "dump -p /system/build.prop $prop_file" "$source_image" >/dev/null 2>&1
fingerprint=$(awk -F= '$1 == "ro.build.fingerprint" { print substr($0, index($0, "=") + 1); exit }' "$prop_file")
if [[ -z "$fingerprint" ]]; then
  fingerprint=$(awk -F= '$1 == "ro.system.build.fingerprint" { print substr($0, index($0, "=") + 1); exit }' "$prop_file")
fi
os_version=$(awk -F= '$1 == "ro.build.version.release" { print substr($0, index($0, "=") + 1); exit }' "$prop_file")
security_patch=$(awk -F= '$1 == "ro.build.version.security_patch" { print substr($0, index($0, "=") + 1); exit }' "$prop_file")
[[ -n "$fingerprint" && -n "$os_version" && -n "$security_patch" ]] || {
  printf 'error: candidate build.prop lacks fingerprint/version/security patch\n' >&2; exit 1;
}

export PATH="$aosp/$host_tools_relative:$PATH"
cp --reflink=auto --preserve=mode,timestamps "$source_image" "$artifact"
python3 "$avbtool" add_hashtree_footer \
  --image "$artifact" \
  --partition_size "$partition_size" \
  --partition_name "$partition_name" \
  --hash_algorithm "$avb_hash_algorithm" \
  --fec_num_roots "$avb_fec_num_roots" \
  --algorithm "$avb_algorithm" \
  --key "$testkey" \
  --rollback_index "$avb_rollback_index" \
  --prop "com.android.build.system.os_version:$os_version" \
  --prop "com.android.build.system.fingerprint:$fingerprint" \
  --prop "com.android.build.system.security_patch:$security_patch"

ln -s "$(basename "$artifact")" "$artifact_dir/system.img"
python3 "$avbtool" verify_image --image "$artifact_dir/system.img" --key "$testkey"
rm -f "$artifact_dir/system.img"
python3 "$avbtool" info_image --image "$artifact" > "$artifact_dir/avb-info.txt"
sha256sum "$source_image" "$artifact" > "$artifact_dir/SHA256SUMS"
{
  printf 'source_image=%s\n' "$source_image"
  printf 'source_sha256=%s\n' "$(sha256sum "$source_image" | awk '{print $1}')"
  printf 'artifact=%s\n' "$artifact"
  printf 'artifact_sha256=%s\n' "$(sha256sum "$artifact" | awk '{print $1}')"
  printf 'partition_size=%s\n' "$partition_size"
  printf 'fingerprint=%s\n' "$fingerprint"
  printf 'security_patch=%s\n' "$security_patch"
} > "$artifact_dir/METADATA.txt"
printf 'Candidate DSU artifact prepared: %s\n' "$artifact"
