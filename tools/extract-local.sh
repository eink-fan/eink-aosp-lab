#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)

usage() {
  printf '%s\n' "usage: $0 --source STOCK_TREE --manifest PRIVATE_MANIFEST --output NEW_DIRECTORY" >&2
  exit 64
}

source_dir=
manifest=
output_dir=
while [ "$#" -gt 0 ]; do
  case "$1" in
    --source) source_dir=${2-}; shift 2 ;;
    --manifest) manifest=${2-}; shift 2 ;;
    --output) output_dir=${2-}; shift 2 ;;
    *) usage ;;
  esac
done

[ -d "$source_dir" ] && [ -f "$manifest" ] && [ -n "$output_dir" ] || usage
if [ -e "$output_dir" ]; then
  printf '%s\n' "refusing to write existing output: $output_dir" >&2
  exit 1
fi
output_parent=$output_dir
while [ ! -e "$output_parent" ]; do output_parent=$(dirname "$output_parent"); done
output_parent=$(CDPATH= cd -- "$output_parent" && pwd -P)
case "$output_parent" in
  "$repo_root"|"$repo_root"/*)
    printf '%s\n' 'private payload output must be outside this repository' >&2
    exit 1 ;;
esac
command -v sha256sum >/dev/null || { printf '%s\n' 'sha256sum is required' >&2; exit 1; }

payload_dir="$output_dir/payload"
mkdir -p "$payload_dir"
lock_file="$output_dir/SHA256SUMS"

while IFS= read -r relative_path || [ -n "$relative_path" ]; do
  case "$relative_path" in
    ''|'#'*) continue ;;
    /*|*'//'*) printf '%s\n' "invalid manifest path" >&2; exit 1 ;;
  esac
  case "/$relative_path/" in
    *'/../'*) printf '%s\n' "parent traversal is not allowed" >&2; exit 1 ;;
  esac
  source_file="$source_dir/$relative_path"
  target_file="$payload_dir/$relative_path"
  if [ ! -f "$source_file" ]; then
    printf '%s\n' "manifest file is absent from source tree" >&2
    exit 1
  fi
  mkdir -p "$(dirname "$target_file")"
  cp -p "$source_file" "$target_file"
  (cd "$payload_dir" && sha256sum "$relative_path") >> "$lock_file"
done < "$manifest"

[ -s "$lock_file" ] || { printf '%s\n' "manifest contained no files" >&2; exit 1; }
printf '%s\n' "staged private local payload; do not commit or publish it"
