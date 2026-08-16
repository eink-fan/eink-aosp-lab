#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/eink-frontlight-extract-test.XXXXXX")
trap 'find "$temp_dir" -depth -delete' EXIT

fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }
fake_bin="$temp_dir/bin"
mkdir "$fake_bin"

cat > "$fake_bin/adb" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

stock_json() {
  python3 - <<'PY'
import json
print(json.dumps({
    "datanum255": list(range(252)),
    "datacold255": [value / 10 for value in range(252)],
}, separators=(",", ":")))
PY
}

if [[ ${1:-} == devices ]]; then
  printf 'List of devices attached\nTESTSERIAL\tdevice\n'
  exit 0
fi
[[ ${1:-} == -s && ${2:-} == TESTSERIAL ]] || exit 2
shift 2
case "${1:-}" in
  get-state)
    printf '%s\n' device ;;
  pull)
    [[ $# == 3 ]]
    printf 'fixture for %s\n' "$2" > "$3" ;;
  shell)
    shift
    if [[ ${1:-} == getprop ]]; then
      case "${2:-}" in
        ro.product.device) printf '%s\n' neo2 ;;
        ro.build.version.release) printf '%s\n' 14 ;;
        ro.build.fingerprint) printf '%s\n' test/fingerprint ;;
        *) exit 2 ;;
      esac
    elif [[ ${1:-} == cmd && ${2:-} == overlay && ${3:-} == lookup ]]; then
      resource=${5##*/}
      case "$resource" in
        brightness_index) seq 0 30 ;;
        brightness_cold_power) seq 0 30 ;;
        brightness_warm_power) seq 0 30 | awk '{ print $1 / 2 }' ;;
        *) exit 2 ;;
      esac
    elif [[ ${1:-} == sh ]]; then
      read -r _request
      stock_json
    else
      exit 2
    fi ;;
  *) exit 2 ;;
esac
EOF
chmod +x "$fake_bin/adb"

payload="$temp_dir/payload"
PATH="$fake_bin:$PATH" "$repo_root/tools/extract-neo2-android17-vendor.sh" \
  --serial TESTSERIAL --output "$payload" >/dev/null
config="$payload/frontlight/neo2_frontlight_calibration.conf"
[[ -f "$config" ]] || fail 'extractor omitted calibration output'
grep -Fxq 'schema=neo2-frontlight-v1' "$config" || fail 'wrong calibration schema'
grep -Eq '^calibration_id=owner-stock-resources-sha256:[0-9a-f]{64}$' "$config" ||
  fail 'calibration identity exposes or omits source state'
grep -Fxq 'max_power_multiple=1.0' "$config" || fail 'neutral power cap missing'
grep -Fxq 'warm_percentage=100' "$config" || fail 'neutral warm multiplier missing'
grep -Fxq 'cold_primary_path=/sys/devices/platform/11009000.i2c/i2c-2/2-0036/b_brightness' \
  "$config" || fail 'cool endpoint missing'
grep -Fxq 'warm_primary_path=/sys/devices/platform/11009000.i2c/i2c-2/2-0036/a_brightness' \
  "$config" || fail 'warm endpoint missing'
if grep -Fq 'enabled=false' "$config"; then fail 'extractor retained disabled placeholder'; fi
python3 "$repo_root/tools/validate-neo2-frontlight-calibration.py" "$config" ||
  fail 'generated calibration failed standalone validation'

for key_and_count in brightness_codes:31 cold_power:31 warm_power:31 drive_codes:252 drive_power:252; do
  key=${key_and_count%%:*}
  expected=${key_and_count#*:}
  value=$(awk -F= -v key="$key" '$1 == key { print $2; found=1 } END { exit found ? 0 : 1 }' \
    "$config") || fail "missing $key"
  actual=$(awk -F, '{ print NF }' <<<"$value")
  [[ "$actual" == "$expected" ]] || fail "$key has $actual entries instead of $expected"
done

custom_model="$temp_dir/frontlight-model.env"
sed -e 's/^max_power_multiple=.*/max_power_multiple=1.25/' \
  -e 's/^warm_percentage=.*/warm_percentage=120/' \
  "$repo_root/profiles/neo2-android17/frontlight-model.env" > "$custom_model"
custom_payload="$temp_dir/custom-payload"
PATH="$fake_bin:$PATH" "$repo_root/tools/extract-neo2-android17-vendor.sh" \
  --serial TESTSERIAL --output "$custom_payload" --frontlight-model "$custom_model" >/dev/null
custom_config="$custom_payload/frontlight/neo2_frontlight_calibration.conf"
grep -Fxq 'max_power_multiple=1.25' "$custom_config" || fail 'custom power cap ignored'
grep -Fxq 'warm_percentage=120' "$custom_config" || fail 'custom warm multiplier ignored'

capture="$temp_dir/invalid-capture.txt"
printf '%s\n' '[android:array/brightness_index]' 0 > "$capture"
if "$repo_root/tools/prepare-neo2-frontlight-calibration.sh" \
    --input "$capture" --output "$temp_dir/invalid.conf" >/dev/null 2>&1; then
  fail 'generator accepted incomplete stock resources'
fi
placeholder="$temp_dir/placeholder.conf"
printf '%s\n' 'schema=neo2-frontlight-v1' 'enabled=false' > "$placeholder"
if python3 "$repo_root/tools/validate-neo2-frontlight-calibration.py" \
    "$placeholder" >/dev/null 2>&1; then
  fail 'validator accepted the former placeholder'
fi

sepolicy="$repo_root/profiles/neo2-android17/optional/frontlight-controls/aosp-patches/system/sepolicy.patch"
grep -Fxq '+allow surfaceflinger neo2_frontlight_cool_sysfs:file { getattr open write };' \
  "$sepolicy" || fail 'optional profile omits exact cool write permission'
grep -Fxq '+allow surfaceflinger neo2_frontlight_warm_sysfs:file { getattr open write };' \
  "$sepolicy" || fail 'optional profile omits exact warm write permission'
if grep -Eq '^\+allow surfaceflinger (sysfs|sysfs_leds):' "$sepolicy"; then
  fail 'optional profile grants generic sysfs access'
fi

grep -Fq 'prepare-neo2-frontlight-calibration.sh' \
  "$repo_root/tools/extract-neo2-android17-vendor.sh" ||
  fail 'owner extractor bypasses calibration validation helper'
grep -Fq 'validate-neo2-frontlight-calibration.py' \
  "$repo_root/tools/setup-neo2-android17.sh" ||
  fail 'workspace setup bypasses complete calibration validation'

printf '%s\n' PASS
