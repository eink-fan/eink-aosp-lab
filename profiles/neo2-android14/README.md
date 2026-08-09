# Neo 2 Android-14 profile

This profile makes the public adapter useful for a Neo 2 owner without
redistributing proprietary runtime files. It covers the same-version Android
14 e-ink library family only. It is not a claim of compatibility with newer
Android releases, a different Neo model, or another e-ink device.

## Known configuration

- Panel input geometry: **1448 × 1072**.
- Full GC16 control hint: **34** (`2 + 32`).
- Normal differential hint: **4**.
- Other observed hints are defined and tested in
  [`neo2_android14_profile.h`](include/einklab/neo2_android14_profile.h).

Hints are passed through `Frame::presentation_hint`. The generic scheduler
does not interpret them; the same-build Neo 2 `Engine` must accept only the
envelopes it has validated.

## Owner-operated setup

1. Keep a working stock installation and a verified return-to-stock plan.
   This repository does not flash, unlock, or alter stock partitions.
2. Use a clean AOSP Android-14 tree matching the device's stock runtime
   family. Do not combine these settings with an arbitrary Android release or
   an unverified vendor library family.
3. Obtain the needed runtime files from your own matching stock image or
   device, where permitted. Maintain the exact file list privately, outside
   this checkout. The public helper stages and integrity-locks that local
   payload without uploading it; see [BYO_VENDOR.md](../../docs/BYO_VENDOR.md).
4. Build the portable core and run its host tests. Implement `einklab::Engine`
   in a private source tree that links only against your locally staged runtime
   and performs its own ABI/provenance checks.
5. Feed that backend only full-panel frames at the starting boundary:

   ```cpp
   frame.dirty_rect = {0, 0,
                       einklab::neo2_android14::kPanelWidth,
                       einklab::neo2_android14::kPanelHeight};
   frame.presentation_hint = einklab::neo2_android14::kFullGc16Hint;
   ```

   Start from the full control path. Add differential operation only after the
   same backend has established correct conversion, buffer ownership, and
   recovery behavior for that device/runtime pair.
6. Prefer a reversible DSU/system-image experiment where supported. Treat
   staging, installation, one boot, visible interaction, and cleanup as
   separate recovery-checked steps.

## Non-negotiable backend rules

- Retain source/pixel ownership until `Engine::Submit` returns; do not reuse a
  buffer that a backend may still read.
- A successful `Submit` is queue acceptance, not physical e-ink completion.
- Reject unknown geometry and hints. Do not silently translate a partial
  rectangle into a full update or vice versa.
- Keep vendor libraries, waveform/calibration files, private headers, payload
  manifests, device logs, and stock images outside this repository.
