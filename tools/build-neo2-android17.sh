#!/usr/bin/env bash
# Build the local Neo 2 Android-17 system image without changing a device.
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
profile="$repo_root/profiles/neo2-android17"
workspace=""
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf 4)
while [[ $# -gt 0 ]]; do
  case "$1" in
    --workspace) workspace=${2:?missing workspace}; shift 2 ;;
    --jobs) jobs=${2:?missing jobs}; shift 2 ;;
    -h|--help) printf '%s\n' "usage: $0 --workspace DIRECTORY [--jobs N]"; exit 0 ;;
    *) printf '%s\n' 'error: unknown option' >&2; exit 64 ;;
  esac
done
[[ -f "$workspace/aosp/build/envsetup.sh" && "$jobs" =~ ^[1-9][0-9]*$ ]] || { printf '%s\n' 'error: invalid prepared workspace or job count' >&2; exit 1; }
lunch_target=$(awk -F= '$1 == "lunch_target" { print $2; exit }' "$profile/profile-contract.env")
[[ "$lunch_target" =~ ^[A-Za-z0-9_.-]+$ ]] || { printf '%s\n' 'error: profile has no valid lunch_target' >&2; exit 1; }
(
  cd "$workspace/aosp"
  # shellcheck disable=SC1091
  source build/envsetup.sh
  lunch "$lunch_target"
  NEO2_M4_RUNTIME_PAYLOAD=true m systemimage -j"$jobs"
)
