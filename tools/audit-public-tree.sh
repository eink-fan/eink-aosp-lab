#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

failed=0
while IFS= read -r -d '' path; do
  case "$path" in
    tools/audit-public-tree.sh) continue ;;
    *.img|*.apk|*.apex|*.so|*.bin|*.mbn|*.dtb|*.dtbo|*.pcap|*.sqlite|*.epub|*.png)
      printf 'prohibited tracked or unignored binary path: %s\n' "$path" >&2
      failed=1
      continue ;;
  esac
  [[ -f "$path" ]] || continue
  if LC_ALL=C grep -InE \
      '(/Users/[^/[:space:]]+|/home/[^/[:space:]]+|[A-Za-z]:\\Users\\|BEGIN (RSA |OPENSSH |EC )?PRIVATE KEY|AKIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9]{20,}|sk-[A-Za-z0-9]{20,}|neo2-aosp-workbench|eink-[0-9]{6}|(^|[^[:alnum:]_])david([^[:alnum:]_]|$)|(^|[^[:alnum:]_])tm4([^[:alnum:]_]|$)|192\.168\.[0-9]{1,3}\.[0-9]{1,3}|10\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3})' \
      "$path" >&2; then
    failed=1
  fi
  if LC_ALL=C grep -InE \
      '(ro\.build\.fingerprint=[^%$[:space:]]|device[_-]?serial[=:][[:space:]]*[A-Za-z0-9_-]{6,}|android_id[=:][[:space:]]*[A-Fa-f0-9]{8,})' \
      "$path" >&2; then
    failed=1
  fi
done < <(git ls-files --cached --others --exclude-standard -z)

if [[ "$failed" -ne 0 ]]; then
  printf 'public-tree privacy audit failed\n' >&2
  exit 1
fi
printf 'Public-tree privacy audit passed.\n'
