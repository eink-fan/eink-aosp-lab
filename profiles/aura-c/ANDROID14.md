# Aura C Android-14 integration

Use Android 14/API 34 on the Aura C Android-11/API-30 vendor and kernel baseline.
Keep the stock vendor and kernel together. The target described here is an
ARM64 userdebug generic system with the model's e-ink binding.

## Source and build

Follow [BUILD.md](BUILD.md) for the owner-payload extraction, pinned checkout,
ordered `base`, `aura` and `controls` patch series, native source graft, current
Ink Controls staging, build and DSU packaging commands. The checked-in resolved
manifest pins each project. The helper supplies the legacy host compatibility
libraries from that checkout and records preparation identity.

This public assembly has not been built or device-tested. The Android-17 setup
script targets Neo 2; use the Aura-specific recipe for this integration.

## Owner-local runtime inputs

Extract these from the matching stock installation into a directory outside
Git. Verify exact file sets and SHA-256 values before staging:

| Stock role | File |
| --- | --- |
| Engine | `libidisplayengine.so` |
| E-ink support | `libeink.so`, `libeink_color.so` |
| Color mapper | `libEink_Kaleido_render_64.so` |
| Gray companion conversion | `libeinkutils.so` |
| Waveforms | `default_wbf.bin`, `regal.wbf` |
| Vendor-side mapper tables | `Kaleido_CFA_LUT.lut`, `AIE_S4.aie`, `VPCOA_LUT.lut` |
| Front-light calibration | Owner-derived `aura_c_frontlight_calibration.conf` |

Obtain runtime libraries from stock system `lib64`, waveforms from stock
runtime resources and tables from the matching vendor resources. Keep tables
on the matching vendor partition during DSU; verify owner-local copies without
packaging redundant tables into system. Use matching ABI declarations and
runtime identities; filenames alone are not compatibility checks.

## Display binding

1. Select `aura_c` explicitly. Set logical portrait geometry 1860 × 2480,
   panel input 2480 × 1860 and density 420. Transform presentation to panel
   once, using the model's 270-degree mapping.
2. Normalize only the exact pre-rotation HWC mode 350 × 1907 on this product.
   Accept already-correct geometry unchanged and reject unknown geometry.
3. Capture owned RGBA snapshots. Check actual pixel format, CPU-lock stride,
   geometry and snapshot lease before copying visible rows into packed storage.
4. Use the Aura six-argument lower-engine call with mapped pixel bytes,
   companion gray, rectangle, mode and two flags. Do not pass a C++ smart
   pointer object as pixels. Keep other runtime call shapes in separate typed
   dispatch paths.
5. Initialize CFA, AIE, VPCOA and engine color/regal support in order. Retain
   table storage for the backend lifetime. Map using the matched VPCOA type-1,
   increment-8 contract and derive the T1000 gray companion from mapped output.
6. Preserve RGB in demand tracking, sleep catalogs and previews. Apply neutral
   treatment and Vivid to fresh source before mapping. Follow the
   [color procedure](../../docs/COLOR_LEARNINGS.md) for paired cleanup.

## Framework and native services

- Keep persistent light state and serialized requests in the framework manager;
  keep calibrated native writes in the single SurfaceFlinger-owned backend.
- Port manager, Settings and paired-slider integration to Android-14's
  View-based SystemUI. Skip the stock slider-scale animation for the custom
  paired view. Commit desired light state on completed gestures.
- Deliver sleep/wake in both Android-14 DisplayPowerController variants. Order
  the final sleep frame and power-off using the backend contract. Restore the
  user's desired light state, including disabled light, on wake.
- Rebind the sleep catalog provider after boot and package replacement. Validate
  generations and owned shared-memory planes; a replaced app process must not
  leave the manager attached to a dead provider.
- In flattened/non-updatable APEX mode, start the packaged DeviceLock service
  before closing system-service startup when the model advertises that feature.
  Preserve ordinary APEX service discovery for other configurations.
- Validate the Bluetooth CommandComplete parent packet before reading its
  opcode, then validate the expected typed response and status.
- Use Android-14-compatible presigned app imports that preserve APK ZIP bytes.
  Inspect final-image executable modes for apps that launch native subprocesses;
  JNI-only libraries do not need executable installation. An owner-approved
  same-signer, same-version data APK update is the established package-manager
  installation route for extracting executable runners.

## Front light

Use the owner-extracted 31-level table for board `SM10D-AKDC`. The two LM3630
controllers at I2C addresses 0036 and 0038 mirror warm/cool channels. Partition
overall intensity between warm/cool step indices, resolve direct observed codes,
and write warm before cool on both controllers. Terminate each sysfs record
with a newline. Grant only the four model-specific primary node permissions.

Inspect `actual_brightness` on all four channels together with framework
Asleep/Awake state. Verify zero while asleep and restored output after waking.
The requested setting and the driver's `brightness` field are not substitutes
for that check. Aura has no pogo/page-turn buttons; do not import those product
capabilities with the light or color binding.

Proceed to [device setup and installation](DEVICE_SETUP.md) only with a verified
image, matching runtime and an owner-local recovery baseline.
