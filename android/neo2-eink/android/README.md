# Android 14 integration

For the distinction between framework targets, model/runtime bindings and
portable color support, see [Android integration targets](../../../docs/ANDROID_INTEGRATION.md).
This historical bridge is also used by the separately patched Android-17
recipe; the directory name does not make it a universal device backend.

This directory contains the Android-specific bridge used by the presentation
adapter. It is compiled into the pinned Android 14 tree and is valid only with
the matching device integration and approved restricted inputs.

The bridge receives Android compositor output, creates an owned frame handoff,
and invokes the retained lower display engine. It deliberately does not model
or reproduce the panel-specific waveform, transport, or power behavior.

## Safety boundary

- Treat this code as same-build private integration, not an app-facing API.
- Keep buffer ownership and synchronization explicit across every handoff.
- Do not treat a successful call as physical display completion.
- Test device changes through the documented DSU flow; do not alter stock
  partitions.

## Maintenance

Keep authored changes and reviewed patches in this repository, then build in
a separate local workspace through the selected profile's documented workflow.
Current build and device procedures live in [`docs/`](../../../docs/);
the portable scheduler design is in [`../DESIGN.md`](../DESIGN.md).
