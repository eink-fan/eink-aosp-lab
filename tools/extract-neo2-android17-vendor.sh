#!/usr/bin/env bash
# Read a locally owned Neo 2 runtime through ADB. It never changes the device.
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
profile="$repo_root/profiles/neo2-android17"
serial=""
output=""
usage() { printf '%s\n' "usage: $0 --output NEW_DIRECTORY [--serial SERIAL]"; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output) output=${2:?missing output path}; shift 2 ;;
    --serial) serial=${2:?missing serial}; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 64 ;;
  esac
done
[[ -n "$output" && ! -e "$output" ]] || { usage >&2; exit 64; }
output_parent=$output
while [[ ! -e "$output_parent" ]]; do output_parent=$(dirname "$output_parent"); done
output_parent=$(CDPATH= cd -- "$output_parent" && pwd -P)
case "$output_parent" in
  "$repo_root"|"$repo_root"/*)
    printf '%s\n' 'error: payload output must be outside this repository' >&2
    exit 64 ;;
esac
command -v adb >/dev/null || { printf '%s\n' 'error: adb is unavailable' >&2; exit 1; }
command -v sha256sum >/dev/null || { printf '%s\n' 'error: sha256sum is unavailable' >&2; exit 1; }
if [[ -z "$serial" ]]; then
  mapfile -t devices < <(adb devices | awk 'NR > 1 && $2 == "device" { print $1 }')
  [[ ${#devices[@]} == 1 ]] || { printf '%s\n' 'error: require one authorized device or --serial' >&2; exit 1; }
  serial=${devices[0]}
fi
adb_cmd=(adb -s "$serial")
[[ $("${adb_cmd[@]}" get-state | tr -d '\r') == device ]] || { printf '%s\n' 'error: device is not ready' >&2; exit 1; }

mkdir -p "$output/payload-system" "$output/payload-system_ext" "$output/waveforms" "$output/frontlight"
getprop_value() { "${adb_cmd[@]}" shell getprop "$1" | tr -d '\r\n'; }
{
  printf 'schema=neo2-payload-source-v1\n'
  printf 'ro.product.device=%s\n' "$(getprop_value ro.product.device)"
  printf 'ro.build.version.release=%s\n' "$(getprop_value ro.build.version.release)"
  printf 'ro.build.fingerprint=%s\n' "$(getprop_value ro.build.fingerprint)"
} > "$output/SOURCE_BUILD.txt"
pull() { mkdir -p "$(dirname "$output/$2")"; "${adb_cmd[@]}" pull "$1" "$output/$2" >/dev/null; }
while IFS= read -r destination || [[ -n "$destination" ]]; do
  [[ -z "$destination" || "$destination" == \#* ]] && continue
  case "$destination" in
    payload-system/*) pull "/system/lib64/${destination#payload-system/}" "$destination" ;;
    payload-system_ext/*) pull "/system_ext/lib64/${destination#payload-system_ext/}" "$destination" ;;
    waveforms/*) pull "/system/etc/${destination#waveforms/}" "$destination" ;;
    *) pull "/system/lib64/$destination" "$destination" ;;
  esac
done < "$profile/vendor-files.txt"

# A panel-only build needs this file present, but a placeholder keeps the
# front-light path disabled until the owner derives their local calibration.
cat > "$output/frontlight/neo2_frontlight_calibration.conf" <<'EOF'
schema=neo2-frontlight-v1
enabled=false
EOF
(
  cd "$output"
  find . -type f ! -name SHA256SUMS -exec sha256sum {} \; | LC_ALL=C sort > SHA256SUMS
)
printf 'Extracted local Neo 2 Android-17 runtime to %s\n' "$output"
