#!/usr/bin/env bash
# Builds only when explicitly invoked by the owner on the prepared Linux host.
set -euo pipefail
workspace=${1:?usage: build-aura-android14.sh WORKSPACE [JOBS]}
jobs=${2:-4}
[[ $(uname -s) == Linux && $(uname -m) == x86_64 && "$jobs" =~ ^[1-9][0-9]*$ ]] || exit 64
workspace=$(cd "$workspace" && pwd)
[[ -f "$workspace/PREPARED.json" && ! -e "$workspace/BUILD.started" ]] || {
  printf '%s\n' 'error: require prepared workspace and a new reviewed build attempt' >&2; exit 1;
}
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 "$repo_root/tools/aura-workspace.py" verify-workspace --workspace "$workspace"
aosp="$workspace/aosp"
legacy="$aosp/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8/sysroot/usr/lib"
printf '%s  %s\n' \
  19494299d25cbb3dd6a19b7de3906b8b33881998801d423deeadc6e2259460f5 "$legacy/libncurses.so.5.9" \
  87ec5f63974b9a4646309fa6e0033ef45b2cdea6c98a319f98596ea613a91ddd "$legacy/libtinfo.so.5.9" \
  2825f0719454a149605bc6df398011a8140732676f54892a764294393a18ed43 "$aosp/prebuilts/build-tools/linux-x86/bin/m4" | sha256sum -c -
compat="$workspace/host-compat"
mkdir "$compat"
ln -s "$legacy/libncurses.so.5.9" "$compat/libncurses.so.5"
ln -s "$legacy/libtinfo.so.5.9" "$compat/libtinfo.so.5"
export M4=out/soong/.intermediates/prebuilts/build-tools/m4/linux_glibc_x86_64/m4
mkdir -p "$(dirname "$aosp/$M4")"
[[ ! -e "$aosp/$M4" && ! -L "$aosp/$M4" ]] || exit 1
ln -s "$aosp/prebuilts/build-tools/linux-x86/bin/m4" "$aosp/$M4"
export LD_LIBRARY_PATH="$compat"
export ALLOW_NINJA_ENV=true
export NEO2_M4_RUNTIME_PAYLOAD=true
(set -o noclobber; printf '%s\n' 'aura-c-android14 systemimage' > "$workspace/BUILD.started")
cd "$aosp"
source build/envsetup.sh
lunch aosp_arm64_aura_c-userdebug
m systemimage -j"$jobs" 2>&1 | tee "$workspace/build.log"
sha256sum out/target/product/generic_arm64/system.img > "$workspace/BUILD.sha256"
