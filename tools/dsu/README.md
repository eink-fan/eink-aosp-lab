# DSU handoff helpers

These helpers are intentionally separate. A successful command does not
authorize the following state transition.

```text
local system.img → AVB/FEC artifact → verified device staging
  → install (disabled) → enable one boot → manual reboot → boot evidence
```

All device-changing operations are limited to Android's temporary DSU state;
they do not write stock partitions. They require explicit acknowledgement
flags and never reboot the device.

## 1. Package the local build

Run after `tools/build-neo2-android17.sh` succeeds. This is host-only and
creates a new artifact directory.

```sh
tools/dsu/prepare-android17-artifact.sh \
  --workspace /path/to/neo2-aosp17 \
  --artifact-dir /path/outside/git/neo2-dsu-artifact
```

It uses the candidate's own build properties when creating and verifying its
AVB/FEC system artifact. Its AOSP output path, AVB key/settings, artifact
name, partition geometry, and DSU size come from the versioned
[`dsu-profile.env`](../../profiles/neo2-android17/dsu-profile.env).

## 2. Stage and verify over ADB

With stock Android running and USB debugging authorized:

```sh
tools/dsu/stage-artifact.sh \
  --artifact /path/outside/git/neo2-dsu-artifact/system-neo2-android17-avb.img
```

This only writes `/data/local/tmp/neo2-dsu/` and verifies its SHA-256; it does
not install, enable, or reboot a DSU.

## 3. Install disabled

Read the artifact hash from `SHA256SUMS`, then deliberately install the staged
file without booting it:

```sh
tools/dsu/install-disabled.sh \
  --remote-image /data/local/tmp/neo2-dsu/system-neo2-android17-avb.img \
  --sha256 <artifact-sha256> \
  --approve-dsu-install
```

If an old temporary DSU is installed, review it and add
`--approve-dsu-wipe-existing` only when replacement is intended.

## 4. Enable exactly one boot

After reviewing disabled-install status, arm one boot only:

```sh
tools/dsu/enable-single-boot.sh --approve-single-boot
```

It does not reboot. Reboot manually only when ready to leave the active stock
session. A normal reboot after a single-boot DSU should return to stock.

## 5. Capture non-invasive boot evidence

After the guest reaches ADB, before visual interaction:

```sh
tools/dsu/capture-boot-evidence.sh \
  --output-dir /path/outside/git/neo2-dsu-evidence/first-boot
```

This records guest identity, boot completion, and SurfaceFlinger/Neo2Eink
diagnostics. The evidence directory may contain device data and must remain
outside Git; the helper rejects any evidence directory inside this repository.

## Aura Android-14 packaging and device actions

The shared artifact packager accepts `--profile profiles/aura-c/dsu-profile.env`
with the device-neutral `eink-dsu-v1` schema; legacy Neo profile schemas remain
supported. Follow [Aura BUILD.md](../../profiles/aura-c/BUILD.md) for exact
packaging arguments. Aura uses the separate staged-file, host-version-aware
[device helper](../device/README.md), with staging, installation and one-boot
enablement as distinct actions. Packaging does not authorize device operations.
