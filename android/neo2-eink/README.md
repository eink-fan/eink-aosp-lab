# Presentation adapter

This directory contains the e-ink presentation path used by the Neo 2 Android
14 DSU bring-up. It divides policy from the device-specific display engine so
that scheduling logic can be tested independently.

## Current status

The same-version Android 14 integration has successfully shown Android
composition on the physical panel, including repeated refreshes. This is a
bring-up result, not a claim that the integration is portable to another
Android release or another device.

## Components

- The portable C++ core owns frame scheduling, coalescing, and policy.
- The Android 14 bridge receives compositor output and passes an explicitly
  owned snapshot to the same-build lower-engine integration.
- The retained lower engine performs panel-specific conversion, waveform, and
  transport work.

The boundary is deliberate: portable policy must not depend on private device
interfaces, while the Android bridge remains tied to the pinned Android 14
source and its matching device inputs.

## Guardrails

- A submitted frame has one clear owner until the lower layer reports it safe
  to release.
- Work may be coalesced, but a buffer is never reused while still in flight.
- The bridge does not infer physical completion from a successful submission.
- System partitions remain untouched; experiments are delivered through DSU.

## Testing the portable core

From this directory:

```sh
cmake -S . -B /tmp/neo2-eink-adapter-build
cmake --build /tmp/neo2-eink-adapter-build --parallel 4
ctest --test-dir /tmp/neo2-eink-adapter-build --output-on-failure
```

For build, deployment, and device procedures, use the reviewed documents in
[`docs/`](../docs/).
