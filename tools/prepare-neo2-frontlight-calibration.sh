#!/usr/bin/env bash
# Convert owner-local, read-only stock resources into the runtime calibration
# consumed by the public front-light backend. No stock bytes enter Git.
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
default_model="$repo_root/profiles/neo2-android17/frontlight-model.env"
input=""
output=""
model="$default_model"

usage() {
  printf '%s\n' \
    "usage: $0 --input STOCK_RESOURCE_CAPTURE --output NEW_FILE [--model MODEL_ENV]"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --input) input=${2:?missing input}; shift 2 ;;
    --output) output=${2:?missing output}; shift 2 ;;
    --model) model=${2:?missing model}; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 64 ;;
  esac
done

[[ -f "$input" && -f "$model" && -n "$output" && ! -e "$output" ]] || {
  usage >&2
  exit 64
}
input_parent=$(CDPATH= cd -- "$(dirname -- "$input")" && pwd -P)
input="$input_parent/$(basename -- "$input")"
case "$input" in
  "$repo_root"|"$repo_root"/*)
    printf '%s\n' 'error: stock calibration capture must remain outside this repository' >&2
    exit 64 ;;
esac
output_parent=$(dirname -- "$output")
[[ -d "$output_parent" ]] || {
  printf 'error: output parent is unavailable: %s\n' "$output_parent" >&2
  exit 64
}
output_parent=$(CDPATH= cd -- "$output_parent" && pwd -P)
output="$output_parent/$(basename -- "$output")"
case "$output" in
  "$repo_root"|"$repo_root"/*)
    printf '%s\n' 'error: generated calibration must remain outside this repository' >&2
    exit 64 ;;
esac
command -v python3 >/dev/null || {
  printf '%s\n' 'error: python3 is required to validate stock calibration data' >&2
  exit 1
}
command -v sha256sum >/dev/null || {
  printf '%s\n' 'error: sha256sum is required' >&2
  exit 1
}

schema=""
max_power_multiple=""
warm_percentage=""
cold_primary_path=""
cold_secondary_path=""
warm_primary_path=""
warm_secondary_path=""
while IFS= read -r line || [[ -n "$line" ]]; do
  [[ -z "$line" || "$line" == \#* ]] && continue
  [[ "$line" == *=* ]] || { printf '%s\n' 'error: malformed front-light model' >&2; exit 1; }
  key=${line%%=*}
  value=${line#*=}
  case "$key" in
    schema) [[ -z "$schema" ]] || exit 1; schema=$value ;;
    max_power_multiple) [[ -z "$max_power_multiple" ]] || exit 1; max_power_multiple=$value ;;
    warm_percentage) [[ -z "$warm_percentage" ]] || exit 1; warm_percentage=$value ;;
    cold_primary_path) [[ -z "$cold_primary_path" ]] || exit 1; cold_primary_path=$value ;;
    cold_secondary_path) cold_secondary_path=$value ;;
    warm_primary_path) [[ -z "$warm_primary_path" ]] || exit 1; warm_primary_path=$value ;;
    warm_secondary_path) warm_secondary_path=$value ;;
    *) printf 'error: unknown front-light model key: %s\n' "$key" >&2; exit 1 ;;
  esac
done < "$model"

valid_endpoint() {
  local path=$1
  [[ "$path" =~ ^/sys/[A-Za-z0-9._:/-]+$ && "$path" != *..* ]]
}
[[ "$schema" == neo2-frontlight-model-v1 &&
   "$max_power_multiple" =~ ^[0-9]+([.][0-9]+)?$ &&
   "$warm_percentage" =~ ^[0-9]+$ &&
   "$warm_percentage" -ge 1 && "$warm_percentage" -le 200 ]] || {
  printf '%s\n' 'error: invalid front-light model policy' >&2
  exit 1
}
python3 - "$max_power_multiple" <<'PY'
import math
import sys

value = float(sys.argv[1])
if not math.isfinite(value) or value <= 0.0 or value > 2.0:
    raise SystemExit("error: max_power_multiple must be greater than 0 and at most 2")
PY
valid_endpoint "$cold_primary_path" && valid_endpoint "$warm_primary_path" || {
  printf '%s\n' 'error: invalid primary front-light endpoint' >&2
  exit 1
}
for endpoint in "$cold_secondary_path" "$warm_secondary_path"; do
  [[ -z "$endpoint" ]] || valid_endpoint "$endpoint" || {
    printf '%s\n' 'error: invalid secondary front-light endpoint' >&2
    exit 1
  }
done

brightness_codes=""
cold_power=""
warm_power=""
drive_codes=""
drive_power=""
while IFS='=' read -r key value; do
  case "$key" in
    brightness_codes) brightness_codes=$value ;;
    cold_power) cold_power=$value ;;
    warm_power) warm_power=$value ;;
    drive_codes) drive_codes=$value ;;
    drive_power) drive_power=$value ;;
    *) printf 'error: unexpected normalized calibration key: %s\n' "$key" >&2; exit 1 ;;
  esac
done < <(python3 - "$input" <<'PY'
import json
import math
import pathlib
import sys

sections = {}
current = None
for raw in pathlib.Path(sys.argv[1]).read_text(encoding="utf-8").splitlines():
    line = raw.strip()
    if line.startswith("[") and line.endswith("]"):
        current = line[1:-1]
        sections[current] = []
    elif current is not None and line:
        sections[current].append(line)

def integer_array(name, size):
    try:
        values = [int(value, 10) for value in sections[name]]
    except (KeyError, ValueError) as error:
        raise SystemExit(f"error: invalid {name} stock resource") from error
    if len(values) != size or any(value < 0 or value > 65535 for value in values):
        raise SystemExit(f"error: invalid {name} stock resource length or range")
    if any(right <= left for left, right in zip(values, values[1:])):
        raise SystemExit(f"error: {name} stock resource is not strictly increasing")
    return values

def power_array(name, size):
    try:
        values = [float(value) for value in sections[name]]
    except (KeyError, ValueError) as error:
        raise SystemExit(f"error: invalid {name} stock resource") from error
    if len(values) != size or any(not math.isfinite(value) or value < 0.0 for value in values):
        raise SystemExit(f"error: invalid {name} stock resource length or range")
    return values

brightness = integer_array("android:array/brightness_index", 31)
cold = power_array("android:array/brightness_cold_power", 31)
warm = power_array("android:array/brightness_warm_power", 31)
try:
    policy = json.loads("\n".join(sections["all255_bright.json"]))
except (KeyError, json.JSONDecodeError) as error:
    raise SystemExit("error: invalid all255_bright.json stock resource") from error

def json_integer_array(name):
    values = policy.get(name)
    if not isinstance(values, list) or len(values) != 252:
        raise SystemExit(f"error: invalid {name} stock policy array")
    if any(isinstance(value, bool) or not isinstance(value, int) or value < 0 or value > 65535
           for value in values):
        raise SystemExit(f"error: invalid {name} stock policy range")
    if any(right <= left for left, right in zip(values, values[1:])):
        raise SystemExit(f"error: {name} stock policy is not strictly increasing")
    return values

def json_power_array(name):
    values = policy.get(name)
    if not isinstance(values, list) or len(values) != 252:
        raise SystemExit(f"error: invalid {name} stock policy array")
    normalized = []
    for value in values:
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise SystemExit(f"error: invalid {name} stock policy value")
        value = float(value)
        if not math.isfinite(value) or value < 0.0:
            raise SystemExit(f"error: invalid {name} stock policy range")
        normalized.append(value)
    return normalized

drive = json_integer_array("datanum255")
drive_power = json_power_array("datacold255")
number = lambda value: format(value, ".17g")
print("brightness_codes=" + ",".join(map(str, brightness)))
print("cold_power=" + ",".join(number(value) for value in cold))
print("warm_power=" + ",".join(number(value) for value in warm))
print("drive_codes=" + ",".join(map(str, drive)))
print("drive_power=" + ",".join(number(value) for value in drive_power))
PY
)
[[ -n "$brightness_codes" && -n "$cold_power" && -n "$warm_power" &&
   -n "$drive_codes" && -n "$drive_power" ]] || {
  printf '%s\n' 'error: stock calibration normalization failed' >&2
  exit 1
}

capture_sha=$(sha256sum "$input" | awk '{print $1}')
{
  printf '%s\n' 'schema=neo2-frontlight-v1'
  printf 'calibration_id=owner-stock-resources-sha256:%s\n' "$capture_sha"
  printf 'max_power_multiple=%s\n' "$max_power_multiple"
  printf 'warm_percentage=%s\n' "$warm_percentage"
  printf 'cold_primary_path=%s\n' "$cold_primary_path"
  printf 'cold_secondary_path=%s\n' "$cold_secondary_path"
  printf 'warm_primary_path=%s\n' "$warm_primary_path"
  printf 'warm_secondary_path=%s\n' "$warm_secondary_path"
  printf 'brightness_codes=%s\n' "$brightness_codes"
  printf 'cold_power=%s\n' "$cold_power"
  printf 'warm_power=%s\n' "$warm_power"
  printf 'drive_codes=%s\n' "$drive_codes"
  printf 'drive_power=%s\n' "$drive_power"
} > "$output"

python3 "$repo_root/tools/validate-neo2-frontlight-calibration.py" "$output"

printf 'Prepared owner-local Neo 2 front-light calibration: %s\n' "$output"
