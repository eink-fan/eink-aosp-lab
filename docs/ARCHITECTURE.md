# Architecture

The project has one hard boundary:

```text
portable presentation policy -> public Engine interface -> same-build Neo 2 backend
```

The portable side may decide when to submit, coalesce, or defer a frame. It
does not know how a panel is driven and must not infer physical completion from
submission acceptance or elapsed time.

The same-build backend source is part of this repository. It owns the
device-specific handles, data formats, power sequencing, and runtime boundary.
The locally extracted waveform and calibration bytes it consumes are not part
of this repository. The public unit tests exercise the scheduler through a
small test engine without simulating real hardware.

This division lets another Android 14 e-ink device reuse or replace the policy
without receiving a misleading promise that any device backend will work.

The `profiles/` directory may add device-specific public configuration such as
geometry and mode hints. It must never contain a proprietary runtime, private
payload manifest. The Neo 2 Android-14 and Android-17 profiles are reference
examples; their framework integrations remain separate.
