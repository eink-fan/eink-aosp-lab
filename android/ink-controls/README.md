# Ink Controls

This directory contains the current authored Java app and offline panel tools.
It supplies Light, sleep-image catalog/preview, refresh, color quality, neutral
preference, Vivid and panel diagnostics. Product capability data lives in
`EinkDeviceProfile.java`; UI and policy remain shared. The package is
`org.neo2.controls`, version code 101. Keep that package for service bindings
and compatible updates. Model names are configuration, not Android versions.

## Build and package

Place this directory in the target AOSP source tree, select the matching
product, then build:

```sh
m InkControls
```

`Android.bp` uses platform APIs, platform signing and privileged installation.
Add `InkControls` to the selected product's `PRODUCT_PACKAGES`, replacing
`Neo2Controls`; do not package both because they share an application ID.
The older app under `android/neo2-eink/android/frontlight-app` stays bound to
the existing public Neo 2 recipe until that recipe's services are updated.

Select `ro.neo2.eink.device_variant=aura_c` for Aura. Other recognized model
profiles have separate geometry and capability flags. Preserve matching native
and framework profiles; a UI property is not runtime compatibility validation.

The app uses source-generated fallback artwork and contains no PNG or bundled
book bytes. Users import their own images through the catalog. Sample-book
provisioning is omitted. HTML/JavaScript panel fixtures run from app assets.

Build against the actual platform, not a public SDK-only `android.jar`; the
app uses hidden platform APIs. A successful host Java policy test does not
compile Android activities or produce an APK. No compiled APK is checked in.

## Required service contract

The app controls the matching system integration; installing it on unmodified
stock does not provide a native e-ink backend.

| Service | Interface / purpose |
| --- | --- |
| `neo2_frontlight` | `android.neo2.INeo2FrontLightManager`, persistent desired light state |
| `neo2_frontlight_controls` | `org.neo2.controls.IFrontLightControl`, native light/refresh/color |
| `neo2_sleep_image_manager` | `android.neo2.INeo2SleepImageManager`, sleep mode and catalog refresh |
| App catalog provider | `org.neo2.controls.ISleepImageCatalog`, generation-checked owned planes |

For the native interface, offsets are relative to `FIRST_CALL_TRANSACTION`:
`+2` quality selection, `+3` refresh request, `+4` gray preference, `+5` refresh
status and `+6` Vivid. The exact request/reply layouts are in `ColorQuality.java`,
`GrayPreference.java`, `DisplayRefresh.java` and `VividColor.java`. Match the
native binder implementation and enforce the signature permissions declared
in the manifest. Do not guess transactions with generic `service call` output.

The framework manager must bind the catalog provider at boot and rebind after
package replacement. Preserve service names, descriptors, generation and
callback layouts. The current public Neo 2 graft does not implement this full
newer service contract; integrate the matching services before enabling these
features in a built image.

## Install and verify an app update

Use an APK built with the installed system's platform signer and a compatible
version. Verify its signer and SHA-256 locally. With an authorized device:

```sh
adb -s <device> install -r <owner-built-InkControls.apk>
```

Substitute the placeholders. Do not use downgrade flags or replace a newer
installed app inadvertently. Verify the installed package path/version and
pulled APK hash. Open Controls and verify native replies, sleep catalog delivery
and the manager's live provider binding. Reboot separately and repeat catalog
delivery. A data APK update does not change the underlying system image.

## Use

- Light: apply brightness/warmth after finishing a slider gesture. Verify the
  desired disabled-light state survives sleep/wake.
- Sleep: import, frame, preview and select owned images. Verify portrait/panel
  orientation and color preservation on Aura.
- Color: compare Original/Balanced/Stronger independently from Vivid and
  cleanup. Confirm the backend accepted the selection. Treatment changes reset
  transition history and request redraw.
- Panel tools: use controlled prior colors, target colors and independent
  animation. Keep explicit refresh, selective cleanup and source treatment
  separate during comparison.
- App-switch refresh: enable deliberately, select its delay and verify settled
  foreground changes. It runs as a foreground service with a static notification;
  stop it when the experiment is complete.

Run the pure-Java checks with `bash tests/run-host-tests.sh`. They exercise
catalog storage/transforms, preference selection and panel command/switch policy.
Run the panel JavaScript checks from this directory with:

```sh
INK_PANEL_ASSET_DIR="$PWD/assets/panel" node tests/comparison.test.js
```
