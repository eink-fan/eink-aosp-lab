#!/usr/bin/env bash
# Install a previously staged DSU image, but never reboot or enable it.
set -euo pipefail

serial=""
remote_image=""
expected_sha256=""
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
profile="$repo_root/profiles/neo2-android17/dsu-profile.env"
gsi_size=""
approved=false
wipe_existing=false
clear_previous_staging=false
clear_staged_image_after_install=false

usage() {
  cat <<'EOF'
usage: install-dsu-staged.sh --remote-image PATH --sha256 HASH [options]

Streams a verified staged image into gsi_tool install --no-reboot. This changes
temporary DSU state but never writes stock partitions, enables the DSU, or
reboots. It requires the exact --approve-dsu-install acknowledgement.

If a previous DSU remains installed, this script refuses to replace it unless
the equally explicit --approve-dsu-wipe-existing acknowledgement is supplied.
The two staging cleanup options are limited to regular .img files in the
reserved /data/local/tmp/neo2-dsu directory; neither can target another path.

Options:
  --serial SERIAL           explicit adb serial (otherwise require exactly one)
  --profile PATH            DSU profile (default: profiles/neo2-android17/dsu-profile.env)
  --approve-dsu-install     acknowledge this temporary DSU installation
  --approve-dsu-wipe-existing
                            wipe an existing disabled DSU before install
  --clear-previous-staging  remove older .img files from the helper staging
                            directory, preserving --remote-image
  --clear-staged-image-after-install
                            remove --remote-image after a verified install
  -h, --help                show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) serial=${2:?missing value for --serial}; shift 2 ;;
    --remote-image) remote_image=${2:?missing value for --remote-image}; shift 2 ;;
    --sha256) expected_sha256=${2:?missing value for --sha256}; shift 2 ;;
    --profile) profile=${2:?missing value for --profile}; shift 2 ;;
    --approve-dsu-install) approved=true; shift ;;
    --approve-dsu-wipe-existing) wipe_existing=true; shift ;;
    --clear-previous-staging) clear_previous_staging=true; shift ;;
    --clear-staged-image-after-install) clear_staged_image_after_install=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) printf 'error: unknown argument: %s\n' "$1" >&2; usage >&2; exit 64 ;;
  esac
done

[[ "$approved" == true ]] || { printf 'error: --approve-dsu-install is required\n' >&2; exit 64; }
[[ -f "$profile" ]] || { printf 'error: DSU profile is absent: %s\n' "$profile" >&2; exit 1; }
profile_value() { awk -F= -v key="$1" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$profile"; }
[[ $(profile_value schema) == neo2-android17-dsu-v1 ]] || { printf '%s\n' 'error: unsupported DSU profile' >&2; exit 1; }
gsi_size=$(profile_value gsi_size)
[[ "$remote_image" = /* && "$remote_image" != *"'"* ]] || {
  printf 'error: --remote-image must be an absolute path without quotes\n' >&2; exit 64;
}
[[ "$expected_sha256" =~ ^[0-9a-fA-F]{64}$ ]] || { printf 'error: --sha256 must be SHA-256\n' >&2; exit 64; }
[[ "$gsi_size" =~ ^[1-9][0-9]*$ ]] || { printf 'error: DSU profile gsi_size must be positive\n' >&2; exit 64; }
command -v adb >/dev/null || { printf 'error: adb is unavailable\n' >&2; exit 1; }
staging_dir=${remote_image%/*}
if [[ "$clear_previous_staging" == true || "$clear_staged_image_after_install" == true ]]; then
  [[ "$staging_dir" == /data/local/tmp/neo2-dsu ]] || {
    printf 'error: staging cleanup is limited to /data/local/tmp/neo2-dsu\n' >&2; exit 64;
  }
fi

if [[ -z "$serial" ]]; then
  devices=()
  while IFS= read -r device; do
    [[ -n "$device" ]] && devices+=("$device")
  done < <(adb devices | awk 'NR > 1 && $2 == "device" { print $1 }')
  [[ ${#devices[@]} -eq 1 ]] || { printf 'error: require exactly one authorized adb device\n' >&2; exit 1; }
  serial=${devices[0]}
fi
adb_cmd=(adb -s "$serial")
root_gsi() {
  "${adb_cmd[@]}" shell su root gsi_tool "$@"
}
root_gsi_shell() {
  "${adb_cmd[@]}" shell su root sh -c "$1"
}
gsid_running=$("${adb_cmd[@]}" shell getprop ro.gsid.image_running | tr -d '\r')
product_device=$("${adb_cmd[@]}" shell getprop ro.product.device | tr -d '\r')
fingerprint=$("${adb_cmd[@]}" shell getprop ro.build.fingerprint | tr -d '\r')
[[ "$gsid_running" != 1 && "$product_device" != generic_arm64 && \
  "$fingerprint" != Android/aosp_arm64/generic_arm64:* ]] || {
  printf 'error: refusing to install while a DSU guest is running\n' >&2; exit 1;
}
actual_sha256=$("${adb_cmd[@]}" shell "sha256sum '$remote_image'" | tr -d '\r' | awk 'NR == 1 { print $1 }')
[[ "$actual_sha256" == "$expected_sha256" ]] || { printf 'error: staged artifact hash does not match --sha256\n' >&2; exit 1; }

printf 'Current DSU status (read-only):\n'
current_status=$(root_gsi status)
printf '%s\n' "$current_status"
if grep -qx 'installed' <<<"$current_status"; then
  [[ "$wipe_existing" == true ]] || {
    printf 'error: an existing DSU is installed; rerun with --approve-dsu-wipe-existing to replace it\n' >&2
    exit 1
  }
  printf 'Wiping the existing temporary DSU before replacement; no stock partition is touched.\n'
  root_gsi wipe
  post_wipe_status=$(root_gsi status)
  printf 'DSU status after wipe:\n%s\n' "$post_wipe_status"
  ! grep -qx 'installed' <<<"$post_wipe_status" || {
    printf 'error: previous DSU still reports installed after wipe\n' >&2; exit 1;
  }
elif [[ "$wipe_existing" == true ]]; then
  printf 'No installed DSU requires a wipe.\n'
fi
if [[ "$clear_previous_staging" == true ]]; then
  printf 'Removing prior helper-staged images while preserving %s.\n' "$remote_image"
  "${adb_cmd[@]}" shell "for stale in '$staging_dir'/*.img; do [ -f \"\$stale\" ] || continue; [ \"\$stale\" = '$remote_image' ] && continue; rm -f \"\$stale\"; done"
  retained_sha256=$("${adb_cmd[@]}" shell "sha256sum '$remote_image'" | tr -d '\r' | awk 'NR == 1 { print $1 }')
  [[ "$retained_sha256" == "$expected_sha256" ]] || {
    printf 'error: staging cleanup did not preserve the selected artifact\n' >&2; exit 1;
  }
fi
printf 'Installing temporary DSU only; no reboot will occur.\n'
root_gsi_shell "gsi_tool install --gsi-size $gsi_size --no-reboot < '$remote_image'"
printf 'Disarming DSU after install to preserve the installed-but-not-enabled boundary.\n'
root_gsi disable
printf 'DSU status after install:\n'
post_status=$(root_gsi status)
printf '%s\n' "$post_status"
grep -qx 'disabled' <<<"$post_status" || {
  printf 'error: DSU did not return to disabled state after install\n' >&2; exit 1;
}
if [[ "$clear_staged_image_after_install" == true ]]; then
  printf 'Removing the now-installed helper-staged image: %s\n' "$remote_image"
  "${adb_cmd[@]}" shell "rm -f '$remote_image'"
  "${adb_cmd[@]}" shell "test ! -e '$remote_image'"
fi
printf 'Installed but not enabled or rebooted. Review status before any separate single-boot approval.\n'
