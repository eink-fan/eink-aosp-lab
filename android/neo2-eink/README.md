# Presentation adapter

This directory contains the authored e-ink presentation and reader-control
source used by the Neo 2 reference integrations. It divides portable policy,
Android services, and the device-specific display boundary so that most
behavior can be tested without vendor software or hardware.

## Current status

The public source includes the conservative panel presenter plus later bounded
sleep-image lifecycle, catalog, preview, overlay, and orientation components.
Host tests cover their pure policy and storage contracts. A host test or
successful submission is not evidence of physical panel completion, and the
public Android profile does not claim that every reference component is wired
or device-qualified.

## Components

- The portable C++ core owns frame scheduling, coalescing, sleep lifecycle,
  overlay composition, and policy.
- The Android bridge receives compositor output and passes an explicitly owned
  snapshot to the same-build lower-engine integration.
- The privileged reference app owns bounded image decode, exact preview,
  generation-guarded catalog storage, and optional cover observation.
- The retained lower engine performs panel-specific conversion, waveform, and
  transport work.

The boundary is deliberate: portable policy must not depend on private device
interfaces, while each Android bridge remains tied to its pinned source and
matching owner-supplied inputs.

## Guardrails

- A submitted frame has one clear owner until the lower layer reports it safe
  to release.
- Work may be coalesced, but a buffer is never reused while still in flight.
- The bridge does not infer physical completion from a successful submission.
- System partitions remain untouched; experiments are delivered through DSU.

## Testing the portable core

From the repository root:

```sh
(
  build_dir=$(mktemp -d "${TMPDIR:-/tmp}/neo2-eink-adapter-build.XXXXXX")
  trap 'rm -rf "$build_dir"' EXIT
  cmake -S android/neo2-eink -B "$build_dir"
  cmake --build "$build_dir" --parallel 4
  ctest --test-dir "$build_dir" --output-on-failure
)
```

The public repository intentionally contains no private build, deployment, or
device-operation record. Use the root profile documentation for its bounded
local build recipe and safety constraints.
