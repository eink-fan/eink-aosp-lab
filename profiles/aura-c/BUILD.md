# Build the Aura C Android-14 system

This recipe includes the pinned per-project AOSP manifest, ordered framework
patches, native adapter, runtime declarations and Ink Controls. It has been
assembled from authored integration source. This public assembly has **not been
built or device-tested**; no build or test was authorized during its preparation.

## Inputs

- Linux x86_64 with Android build prerequisites, `repo`, Git, Python 3 and at
  least 300 GiB free for a fresh checkout and output. Local loopback must work.
- Stock Aura C (`ATILIM_mPAD07`, API 30), authorized ADB, and permission to read
  its runtime. Extraction reads device files; it does not root or reboot it.
- A JSON array containing the owner's 31 stock brightness codes for board
  `SM10D-AKDC`. Extract the common warm/cool table from that board's stock UI
  resources; retain its exact increasing integer codes, beginning at zero.
  This table is an owner-local vendor input, not a synthetic calibration.

Libraries, waveform and mapper-table hashes are locked in
`vendor-files.sha256`. Source identity is model/API only; no serial, fingerprint
or owner identity is embedded in the public recipe. Calibration is generated
from the supplied table with the four exact model endpoints and newline writes.

## Extract and prepare

From the repository, substitute owner-local paths:

```sh
python3 tools/aura-workspace.py extract --serial <device> \
  --levels /path/outside/git/aura-levels.json \
  --output /path/outside/git/aura-payload

python3 tools/aura-workspace.py verify-payload \
  --payload /path/outside/git/aura-payload

python3 tools/aura-workspace.py prepare \
  --payload /path/outside/git/aura-payload \
  --workspace /path/outside/git/aura-aosp14 --jobs 4
```

`prepare` requires a new workspace. It checks the host before downloading,
syncs the pinned per-project manifest, verifies project HEADs, applies `base`,
`aura`, then `controls` patches, and stages `android/device-eink` plus
`android/ink-controls` at the SurfaceFlinger graft. It records the complete
graft/payload hashes and patched project state in `PREPARED.json`.

The `controls` patch supplies manager rebinding after APK replacement. The
product installs `InkControls` instead of the old `Neo2Controls`. Third-party
app packages and forced browser/home choices are not required by this recipe;
install owner-selected applications separately.

A failed preparation remains incomplete and has no valid terminal preparation
receipt. Inspect it; do not point the tool at an existing working tree or erase
one to bypass the new-workspace requirement.

## Build when separately authorized

```sh
bash tools/build-aura-android14.sh /path/outside/git/aura-aosp14 4
```

The driver verifies prepared source, selects `aosp_arm64_aura_c-userdebug`,
enables the owner-local runtime package and builds `systemimage`. It binds
the pinned legacy ncurses/tinfo/M4 tools and their actual Ninja environment.
It creates `BUILD.started`, retains `build.log`, and writes `BUILD.sha256` only
after the build command succeeds. A repeated attempt requires reviewing and
retaining the previous attempt's marker/log; the driver does not retry it.

The source image is `aosp/out/target/product/generic_arm64/system.img`.
Successful image production is separate from device qualification.

## Package for DSU

The shared AVB/FEC packager accepts this profile explicitly; its historical
script filename does not select Android-17 source when `--profile` is supplied:

```sh
bash tools/dsu/prepare-android17-artifact.sh \
  --workspace /path/outside/git/aura-aosp14 \
  --profile "$PWD/profiles/aura-c/dsu-profile.env" \
  --artifact-dir /path/outside/git/aura-dsu
```

The Aura package uses a 3-GiB partition envelope, including AVB/FEC headroom;
the packager rejects an image that does not fit. Record its hash/size separately
from the raw image. Continue with [device tools](../../tools/device/README.md).

## Presentation mode

The presenter and continuous submission are enabled by default in every build
variant, without a lifetime update cap. Pacing, demand gating and paired-color
ownership checks remain active. Set `debug.neo2.eink.continuous=0` for the finite
diagnostic budget (12 calls by default, configurable up to 24); paired cleanup
reserves both calls before starting. Use `debug.neo2.eink.enabled=0` for
capture-only diagnosis. Both overrides are volatile. New model profiles must
qualify their own default; color capability or Android version does not opt in.

The native/framebuffer, Android framework, color and light contracts are in
[ANDROID14.md](ANDROID14.md). See [feature coverage](../../docs/FEATURES.md)
before selecting a different device or source graft.
