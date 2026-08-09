#!/usr/bin/env bash
# Collect read-only proof after a deliberately approved DSU boot.
set -euo pipefail

serial=""
output_dir=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) serial=${2:?missing value for --serial}; shift 2 ;;
    --output-dir) output_dir=${2:?missing value for --output-dir}; shift 2 ;;
    -h|--help)
      printf '%s\n' 'usage: capture-dsu-boot-evidence.sh --output-dir DIRECTORY [--serial SERIAL]'
      exit 0 ;;
    *) printf 'error: unknown argument: %s\n' "$1" >&2; exit 64 ;;
  esac
done

[[ -n "$output_dir" && ! -e "$output_dir" ]] || {
  printf 'error: --output-dir must name a new directory\n' >&2; exit 64;
}
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
output_parent=$output_dir
while [[ ! -e "$output_parent" ]]; do output_parent=$(dirname "$output_parent"); done
output_parent=$(CDPATH= cd -- "$output_parent" && pwd -P)
case "$output_parent" in
  "$repo_root"|"$repo_root"/*)
    printf '%s\n' 'error: evidence output must be outside this repository' >&2
    exit 64 ;;
esac
command -v adb >/dev/null || { printf 'error: adb is unavailable\n' >&2; exit 1; }
if [[ -z "$serial" ]]; then
  devices=()
  while IFS= read -r device; do
    [[ -n "$device" ]] && devices+=("$device")
  done < <(adb devices | awk 'NR > 1 && $2 == "device" { print $1 }')
  [[ ${#devices[@]} -eq 1 ]] || { printf 'error: require exactly one authorized adb device\n' >&2; exit 1; }
  serial=${devices[0]}
fi
mkdir -p "$output_dir"
adb_cmd=(adb -s "$serial")
"${adb_cmd[@]}" get-state > "$output_dir/adb-state.txt"
"${adb_cmd[@]}" shell 'getprop ro.gsid.image_running; getprop sys.boot_completed; getprop dev.bootcomplete; getprop ro.build.fingerprint; getprop ro.adb.secure' \
  > "$output_dir/identity-and-boot-properties.txt"
"${adb_cmd[@]}" shell 'logcat -d -b all -v threadtime -s SurfaceFlinger Neo2Eink* *:S' \
  > "$output_dir/surfaceflinger-and-neo2eink.log" || true
"${adb_cmd[@]}" shell 'dumpsys SurfaceFlinger --neo2-eink' \
  > "$output_dir/neo2-eink-diagnostics.txt" || true
"${adb_cmd[@]}" shell 'dumpsys SurfaceFlinger' > "$output_dir/surfaceflinger-dumpsys.txt" || true
printf 'Read-only DSU boot evidence captured at %s\n' "$output_dir"
