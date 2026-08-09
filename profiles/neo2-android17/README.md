# Neo 2 Android-17 profile

This profile records the Neo 2 Android-17/API-37 binding boundary. It is a
separate framework integration from Android 14, even though it uses the same
locally supplied e-ink runtime family and observed mode envelope. Do not copy
an Android-14 framework patch into Android 17 without rebasing and reviewing
it against the target source.

## Known configuration and evidence

- Panel input geometry: **1448 × 1072**.
- Full GC16 control hint: **34** (`2 + 32`).
- Normal differential hint: **4**.
- The other observed hints are defined and tested in
  [`neo2_android17_profile.h`](include/einklab/neo2_android17_profile.h).

This is an experimental Android-17 generic ARM64 userdebug GSI / DSU profile,
not a full Neo 2 device port. A successful local build is not evidence of
boot, panel output, recovery, sleep/wake, OTA, battery, thermal behavior, or
daily-driver suitability.

## Local workstation build

1. Use a Linux x86_64 workstation with Android build prerequisites, network
   access, and enough free disk for a full checkout and output tree. Check it
   before syncing:

   ```sh
   tools/check-host-android17.sh
   ```

2. Preserve stock as the recovery baseline. Enable and authorize ADB on the
   Neo 2. Root ADB is helpful for investigation but not required for the
   standard read-only system-file extraction.
3. Extract your own matching Android-14 Neo 2 runtime to a new local
   directory:

   ```sh
   tools/extract-neo2-android17-vendor.sh --output /path/outside/git/neo2-payload
   ```

4. Create and patch a local Android-17/API-37 checkout, then stage that local
   payload into the graft:

   ```sh
   tools/setup-neo2-android17.sh \
     --workspace /path/to/new/neo2-aosp17 \
     --payload /path/outside/git/neo2-payload
   tools/build-neo2-android17.sh --workspace /path/to/new/neo2-aosp17
   ```

   The setup script verifies every payload byte against
   [`vendor-files.sha256`](vendor-files.sha256), verifies the Android-14 Neo 2
   source record against [`payload-source.env`](payload-source.env), resolves
   AOSP through the immutable superproject named in
   [`aosp-manifest-source.env`](aosp-manifest-source.env), writes the complete
   resolved project manifest and hash beside the workspace, applies the
   framework/build/SELinux patches, installs the authored `Neo2Eink` source
   graft, and stages only the user-extracted vendor bytes.
5. The first supported presentation boundary is a full-panel frame using
   `kFullGc16Hint`:

   ```cpp
   frame.dirty_rect = {0, 0,
                       einklab::neo2_android17::kPanelWidth,
                       einklab::neo2_android17::kPanelHeight};
   frame.presentation_hint = einklab::neo2_android17::kFullGc16Hint;
   ```

6. The produced image is only a build artifact. Use the guarded
   [DSU handoff helpers](../../tools/dsu/README.md) for packaging, staging,
   disabled install, one-boot enablement, and post-boot evidence. Installation,
   one boot, interaction, and cleanup are distinct recovery-checked operations.

## Backend rules

- Reject an unknown geometry, hint, or runtime family.
- The confirmed full-binding direct presenter is enabled by default for every
  build variant. After a separately authorized DSU boot and guest-ADB
  inspection, an operator may deliberately disarm it for a capture-only
  diagnostic boot with:

  ```sh
  adb shell setprop debug.neo2.eink.enabled 0
  ```

  This volatile property is cleared by reboot. Continuous submission remains
  off by default with a finite diagnostic budget and must stay off except
  during a separately approved, directly supervised experiment.
- The public profile intentionally grants SurfaceFlinger no front-light write
  access. The extracted placeholder configuration is fail-closed. Front-light
  control is not a supported outcome of this profile.
- Treat submission acceptance and elapsed time only as queue evidence, never
  physical panel completion.
- Do not make extracted vendor libraries, waveform/calibration data, stock
  images, logs, or device identifiers part of this repository.
