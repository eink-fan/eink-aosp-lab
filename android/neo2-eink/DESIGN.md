# Presentation adapter design

## Purpose

The adapter makes Android composition usable by the Neo 2 e-ink stack while
keeping display policy reviewable and testable. It is a same-device,
same-Android-14 integration; only the scheduler and policy core are intended
to be portable.

## Data path

```text
Android compositor
  -> owned frame snapshot and synchronization data
  -> policy scheduler
  -> Android 14 lower-engine bridge
  -> retained device display engine
  -> panel
```

The bridge gives the lower engine a validated input it can consume. It does
not replace the panel conversion, waveform selection, power sequencing, or
transport machinery retained with the device integration.

## Ownership and completion

Each frame moves through three states:

1. Captured: the bridge has an owned snapshot and its synchronization state.
2. Submitted: the lower engine has accepted work; the snapshot remains owned
   and cannot be reused.
3. Released: an explicit completion path makes reuse safe.

The scheduler may replace pending work with a newer frame, but it must never
overwrite submitted work. Submission success is not proof that the panel has
finished updating.

## Policy boundary

The portable core decides when and what to submit: rate limiting, coalescing,
interactive versus idle behavior, and recovery after a failed submission. The
Android bridge performs only the narrow same-build handoff required to carry a
snapshot to the retained engine.

This separation keeps future policy work testable without exposing private
device interfaces to general-purpose code.

## Current and next work

The proven configuration refreshes continuously, which established the full
composition-to-panel path. The next milestone is a bounded demand-driven loop:
submit after meaningful compositor changes, coalesce bursts, and retain a
safe fallback when completion cannot be established. Follow the active plan
and status documents for milestone state rather than older diagnostic notes.
