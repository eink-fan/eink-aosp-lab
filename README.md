# E-ink AOSP lab

A small, device-neutral reference repository for experimenting with an e-ink
presentation policy on Android-derived systems. It shares portable policy,
test scaffolding, and a deliberately narrow backend seam. It does **not**
ship a device image, vendor implementation, or proprietary payload.

This is a fail-closed bring-up aid for an experimental generic-system-image
(GSI) / DSU attempt, not a daily-driver distribution or a full device port.
It does not claim that one device's e-ink engine is compatible with another.

## What is here

- A complete portable C++ presentation adapter: asynchronous latest-frame
  scheduling, snapshot ownership, admission/resample policies, grayscale
  conversion/delta analysis, readiness gates, and host tests.
- The reviewed Neo 2 Android source integration, including the same-build
  engine bridge, bounded sleep-image catalog and overlay policies, and a
  conservative Android-17 AOSP patch set.
- Android-14 and Android-17 Neo 2 profiles with observed panel geometry and
  refresh-mode hints, plus same-build integration guides.
- A safe local-only payload staging helper.
- Porting, safety, and public-release guidance.

## What is intentionally absent

- Vendor libraries, waveform/calibration bytes, stock images, OTAs, partition
  dumps, device captures, and credentials.
- Personal paths, host or account names, device-instance identifiers, build
  receipts, and private operational history.
- Flashing, unlocking, or stock-partition modification procedures.

## Quick host check

```sh
cmake -S . -B /tmp/eink-aosp-lab-build
cmake --build /tmp/eink-aosp-lab-build
ctest --test-dir /tmp/eink-aosp-lab-build --output-on-failure
```

## What a new builder needs

- Linux x86_64 workstation with network access, Android `repo`, JDK/toolchain,
  and enough disk for a full AOSP checkout plus build output.
- A Neo 2 on stock Android, connected over authorized USB ADB.
- This repository. No other project checkout, remote host, or private script
  is required.

The only untracked project input is the vendor runtime extracted locally from
the owner’s device. The same read-only extraction derives an editable
front-light calibration from active stock resources; no calibration table or
device record is committed. Setup rejects a payload that is not the profile's
locked Android-14 Neo 2 runtime, pins the complete AOSP checkout through an
immutable superproject commit, and records the resolved per-project manifest
beside the workspace. Check the host, extract, create the workspace, and build:

```sh
tools/check-host-android17.sh
tools/extract-neo2-android17-vendor.sh --output /path/outside/git/neo2-payload
tools/setup-neo2-android17.sh \
  --workspace /path/to/new/neo2-aosp17 \
  --payload /path/outside/git/neo2-payload
tools/build-neo2-android17.sh --workspace /path/to/new/neo2-aosp17
```

This produces a build artifact only. The confirmed full-binding e-ink
presenter is enabled by default; continuous submission remains off and bounded
by a finite diagnostic budget. The base public profile cannot write front-light
sysfs nodes unless the optional control-surface bundle is selected.
That bundle grants access only to the two model-typed primary nodes and consumes
the owner-local calibration; it remains experimental and does not provide
hardware readback. DSU installation, one-boot enablement, reboot, and
interaction are separate deliberate steps; see
[the DSU handoff guide](tools/dsu/README.md).

The authored source also contains newer host-tested catalog, preview, overlay,
and portrait-to-panel transformation components. They are included for review
and porting, but the conservative public profile does not claim that every
reference component is active or device-qualified in its produced image.
An opt-in [front-light control-surface proposal](profiles/neo2-android17/optional/frontlight-controls/README.md)
publishes the manager, Settings, and SystemUI architecture and enables the
narrow writer when owner-local calibration extraction succeeds.

## Before publishing

The independently authored project material is dedicated to the public domain
under [The Unlicense](LICENSE). It may be used for any purpose. This dedication
does not grant rights to proprietary vendor software or other third-party
material; keep those inputs local as described in
[docs/BYO_VENDOR.md](docs/BYO_VENDOR.md).

We welcome an OEM-supported path to modern Android for Neo 2 owners.

Start with [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
[docs/BYO_VENDOR.md](docs/BYO_VENDOR.md), and
[docs/PORTING.md](docs/PORTING.md). See [docs/CHANGELOG.md](docs/CHANGELOG.md)
for the public source history. Neo 2 owners should then read
[the Android-14 profile](profiles/neo2-android14/README.md) or
[the Android-17 profile](profiles/neo2-android17/README.md).
