# Portable presentation adapter

`PresentationAdapter` is the center of the library. It owns a serial worker,
accepts frames only while started, keeps at most one pending frame, and replaces
that pending frame with the newest candidate. It never interrupts a submission
already handed to `Engine`.

The supporting components are independent and can be adopted selectively:

- `SnapshotPool`: fixed non-blocking ownership leases for copied source frames.
- `FrameDemandGate`, `MicroDeltaPolicy`, and `CaptureAdmissionPolicy`: avoid
  needless work while keeping an explicit conservative fallback.
- `FullPoolResamplePolicy` and `MainThreadResampleDispatcher`: coalesce one
  recovery request when a bounded capture pool is full.
- `ConvertToGrayscale`, delta, and fidelity helpers: retain no pixels beyond
  the caller-owned grayscale buffer.
- `IdleGate` and `SubmissionObserver`: model bounded readiness and
  queue-acceptance observation without treating either as panel completion.

The library deliberately leaves device modes as `Frame::presentation_hint`.
The Android-17 Neo 2 backend in `android/neo2-eink/` assigns the documented
Neo-specific meanings; another device must provide its own mapping.
