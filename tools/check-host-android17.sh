#!/usr/bin/env bash
# Check only the local workstation prerequisites for an Android-17 build.
set -euo pipefail

failed=0
need() {
  if command -v "$1" >/dev/null; then
    printf 'ok: %s\n' "$1"
  else
    printf 'missing: %s\n' "$1" >&2
    failed=1
  fi
}

if [[ $(uname -s) != Linux ]]; then
  printf '%s\n' 'missing: a Linux build host is required for this AOSP workflow' >&2
  failed=1
fi
if [[ $(uname -m) != x86_64 && $(uname -m) != amd64 ]]; then
  printf '%s\n' 'missing: an x86_64 build host is required for this AOSP workflow' >&2
  failed=1
fi
for command in adb repo git python3 java javac make gcc g++ bison flex zip unzip curl perl rsync \
               file git-lfs bc ccache lz4 zstd; do
  need "$command"
done
for command in debugfs sha256sum; do
  if ! command -v "$command" >/dev/null; then
    printf 'warning: %s is required later for DSU artifact packaging\n' "$command" >&2
  fi
done

if command -v df >/dev/null; then
  available_kib=$(df -Pk . | awk 'NR == 2 {print $4}')
  if [[ ${available_kib:-0} =~ ^[0-9]+$ ]]; then
    available_gib=$((available_kib / 1024 / 1024))
    printf 'available workspace storage: %s GiB\n' "$available_gib"
    if (( available_gib < 300 )); then
      printf '%s\n' 'warning: less than 300 GiB free; a full AOSP checkout and build may not fit' >&2
    fi
  fi
fi

if (( failed )); then
  printf '%s\n' 'Install the missing host prerequisites, then rerun this check before repo sync.' >&2
  exit 1
fi
printf '%s\n' 'Host preflight passed. AOSP source and vendor payload have not been downloaded.'
