# Device and recipe coverage

These columns describe source wired into each public recipe. They are not a
claim that the newly assembled Aura recipe has been built or device-qualified.
That work was explicitly excluded from this source transfer.

| Feature | Neo 2 / Android 17 | Aura C / Android 14 |
| --- | --- | --- |
| Pinned source and setup | Existing `setup-neo2-android17.sh` | New `aura-workspace.py prepare`, pinned per-project manifest |
| Native source graft | `android/neo2-eink` | `android/device-eink` |
| Panel-native geometry | 1448 × 1072 | 2480 × 1860, exact model HWC correction |
| Ordinary display | Matched monochrome engine | Matched CFA color + T1000 gray companion |
| Presentation default | Enabled, continuous; no lifetime cap | Enabled, continuous; no lifetime cap |
| Continuous output | On by default; finite diagnostic override available | On by default; finite diagnostic override available |
| Color treatment / paired cleanup | Not a monochrome capability | RGB tracking, neutral preference, Vivid, exact-marker protected cleanup |
| Light | Optional two-node control bundle with owner calibration | Four primary nodes, mirrored warm/cool, owner stock-code table |
| Current Ink Controls | Available separately; not substituted into the older recipe | Packaged with matching native transactions and manager rebind patch |
| Sleep catalog and lifecycle | Reference components present; conservative recipe does not qualify all of them | Framework/power-controller integration, RGB catalog, preview and rebind included |
| Android-specific runtime repairs | Existing pinned Android-17 patches | DeviceLock, Bluetooth validation, battery labeling and charger-wake filtering included |
| Temporary DSU | Existing guarded helpers | New host-version-aware staged-file helper |
| Permanent installation | Neo-specific stock-B/AVB setup and standard-fastboot system-A procedure | Recovery preparation, baseline/readback capture and system-A writer supplied |
| Proprietary inputs | Owner-extracted matched Neo runtime | Owner-extracted matched Aura runtime, waveforms, tables and light codes |

Use [the Neo 2 profile](../profiles/neo2-android17/README.md) or
[the Aura recipe](../profiles/aura-c/BUILD.md). Do not replace one source graft
with the other simply because the module names overlap. The newer native
snapshot contains model capability definitions for additional products; those
definitions alone do not provide supported build/device recipes for them.

Read [Neo 2 stock preparation and promotion](../profiles/neo2-android17/DEVICE_SETUP.md)
or [Aura device setup](../profiles/aura-c/DEVICE_SETUP.md) before permanent work.

The remaining evidence requirement for the Aura public assembly is an
owner-authorized fresh build and per-feature device qualification. Host-only
checks from earlier component work do not establish that result. The exact
Aura unlock procedure is not established in the available authored records.
