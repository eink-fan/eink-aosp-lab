# Android 14 integration

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

Make Android changes in the reproducible remote workspace, export reviewed
patches to this control repository, then build and test through the documented
workflow. Current build and device procedures live in [`docs/`](../../docs/);
the portable scheduler design is in [`../DESIGN.md`](../DESIGN.md).
