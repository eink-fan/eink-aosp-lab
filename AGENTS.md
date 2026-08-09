# Agent instructions

This repository is a self-contained local Android-17 Neo 2 build recipe. Its
only intentionally missing inputs are vendor-owned bytes extracted by the
device owner from their own USB-connected Neo 2. Do not fetch, commit, upload,
or redistribute those bytes.

## First read

1. `README.md`
2. `profiles/neo2-android17/README.md`
3. `tools/dsu/README.md` before any DSU operation
4. `docs/SAFETY.md` and `docs/BYO_VENDOR.md`

## Build-host contract

The supported build host is Linux x86_64 with network access, Android's
`repo` tool, a JDK/toolchain suitable for the pinned Android source, and ample
free storage for a full checkout plus build output. Run:

```sh
tools/check-host-android17.sh
```

before downloading AOSP. The host preflight reports missing commands and low
disk space; it does not install packages or change the host.

## Standard local workflow

With stock Android running and ADB authorized:

```sh
tools/extract-neo2-android17-vendor.sh --output /path/outside/git/neo2-payload
tools/setup-neo2-android17.sh \
  --workspace /path/to/new/neo2-aosp17 \
  --payload /path/outside/git/neo2-payload
tools/build-neo2-android17.sh --workspace /path/to/new/neo2-aosp17
```

The extractor is read-only with respect to the device. Setup verifies the
payload checksum lock and its Android-14 Neo 2 source identity, resolves the
entire AOSP checkout through the profile's immutable superproject commit, and
writes `aosp-resolved-manifest.xml` plus its hash beside the workspace. It then
applies the versioned AOSP patches, copies the authored source graft, and
stages only the owner-extracted payload. The build script produces
`systemimage`; none of these steps installs, enables, or reboots a DSU.

## Boundaries

- Keep `payload/`, `workspaces/`, `artifacts/`, and `evidence/` outside Git.
- A successful build is not proof of boot, panel output, physical completion,
  front-light behavior, recovery, or daily-driver suitability.
- The confirmed full-binding direct presenter is on by default in every build
  variant. Continuous submission remains off and bounded; do not enable it
  without a separately authorized, directly supervised experiment. An
  operator may set the documented volatile property to `0` for a capture-only
  diagnostic boot.
- The extractor creates a fail-closed front-light placeholder so a panel build
  can proceed without inventing calibration. This public profile also grants
  no SurfaceFlinger front-light write permission; do not claim front-light
  support or re-add that permission without a separately confined design.
- Do not alter stock partitions. A DSU install, one-boot enablement, manual
  reboot, visible interaction, evidence capture, and DSU cleanup are separate
  decisions. Follow `tools/dsu/README.md`; never chain them automatically.

## Development and verification

Run both host suites after C++ changes:

```sh
cmake -S . -B /tmp/eink-aosp-lab-build
cmake --build /tmp/eink-aosp-lab-build --parallel 4
ctest --test-dir /tmp/eink-aosp-lab-build --output-on-failure

cmake -S android/neo2-eink -B /tmp/eink-aosp-lab-neo2-adapter-build
cmake --build /tmp/eink-aosp-lab-neo2-adapter-build --parallel 4
ctest --test-dir /tmp/eink-aosp-lab-neo2-adapter-build --output-on-failure
```

For shell edits, run `bash -n` on every changed script. Before publishing,
complete `docs/PUBLIC_RELEASE.md` and select a project license.
