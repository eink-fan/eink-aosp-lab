#!/usr/bin/env bash
# Arm an already-installed DSU for one boot; deliberately does not reboot.
set -euo pipefail

serial=""
approved=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) serial=${2:?missing value for --serial}; shift 2 ;;
    --approve-single-boot) approved=true; shift ;;
    -h|--help)
      printf '%s\n' 'usage: enable-dsu-single-boot.sh [--serial SERIAL] --approve-single-boot'
      exit 0 ;;
    *) printf 'error: unknown argument: %s\n' "$1" >&2; exit 64 ;;
  esac
done

[[ "$approved" == true ]] || { printf 'error: --approve-single-boot is required\n' >&2; exit 64; }
command -v adb >/dev/null || { printf 'error: adb is unavailable\n' >&2; exit 1; }
if [[ -z "$serial" ]]; then
  devices=()
  while IFS= read -r device; do
    [[ -n "$device" ]] && devices+=("$device")
  done < <(adb devices | awk 'NR > 1 && $2 == "device" { print $1 }')
  [[ ${#devices[@]} -eq 1 ]] || { printf 'error: require exactly one authorized adb device\n' >&2; exit 1; }
  serial=${devices[0]}
fi
adb -s "$serial" shell su root gsi_tool enable --single-boot
adb -s "$serial" shell su root gsi_tool status
printf 'DSU is armed for one boot. This script intentionally does not reboot the reader.\n'
