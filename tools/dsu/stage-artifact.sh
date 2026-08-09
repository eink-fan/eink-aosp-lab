#!/usr/bin/env bash
# Copy one full DSU artifact to /data/local/tmp and verify the device-side hash.
set -euo pipefail

artifact=""
serial=""
remote_dir=/data/local/tmp/neo2-dsu

usage() {
  cat <<'EOF'
usage: stage-dsu-artifact.sh --artifact PATH [options]

Push one complete artifact with adb, then compare its device-side SHA-256 to
the local file. This writes only the named staging directory under /data; it
does not invoke gsi_tool, enable a DSU, reboot, or touch stock partitions.

Options:
  --serial SERIAL      explicit adb serial (otherwise require exactly one)
  --remote-dir PATH    staging directory (default: /data/local/tmp/neo2-dsu)
  -h, --help           show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --artifact) artifact=${2:?missing value for --artifact}; shift 2 ;;
    --serial) serial=${2:?missing value for --serial}; shift 2 ;;
    --remote-dir) remote_dir=${2:?missing value for --remote-dir}; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) printf 'error: unknown argument: %s\n' "$1" >&2; usage >&2; exit 64 ;;
  esac
done

[[ -f "$artifact" ]] || { printf 'error: artifact is absent: %s\n' "$artifact" >&2; exit 1; }
[[ "$remote_dir" = /* && "$remote_dir" != *"'"* ]] || {
  printf 'error: --remote-dir must be an absolute path without quotes\n' >&2; exit 64;
}
command -v adb >/dev/null || { printf 'error: adb is unavailable\n' >&2; exit 1; }
command -v sha256sum >/dev/null || { printf 'error: sha256sum is unavailable\n' >&2; exit 1; }

if [[ -z "$serial" ]]; then
  devices=()
  while IFS= read -r device; do
    [[ -n "$device" ]] && devices+=("$device")
  done < <(adb devices | awk 'NR > 1 && $2 == "device" { print $1 }')
  [[ ${#devices[@]} -eq 1 ]] || {
    printf 'error: require exactly one authorized adb device; use --serial if needed\n' >&2; exit 1;
  }
  serial=${devices[0]}
fi
adb_cmd=(adb -s "$serial")
filename=$(basename "$artifact")
remote_image="$remote_dir/$filename"
local_hash=$(sha256sum "$artifact" | awk '{print $1}')

printf 'Device storage before staging:\n'
"${adb_cmd[@]}" shell "df -h '$remote_dir' 2>/dev/null || df -h /data"
"${adb_cmd[@]}" shell "mkdir -p '$remote_dir'"
"${adb_cmd[@]}" push "$artifact" "$remote_image"
remote_hash=$("${adb_cmd[@]}" shell "sha256sum '$remote_image'" | tr -d '\r' | awk 'NR == 1 { print $1 }')
[[ "$remote_hash" == "$local_hash" ]] || {
  printf 'error: device-side SHA-256 mismatch; staged file remains for diagnosis\n' >&2; exit 1;
}
printf 'Verified staged artifact: %s on %s\n' "$remote_image" "$serial"
