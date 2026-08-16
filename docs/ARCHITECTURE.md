# Architecture

The project has two hard boundaries:

```text
portable presentation policy -> public Engine interface -> same-build Neo 2 backend
bounded reader data -> generation-checked catalog interface -> presentation policy
```

The portable side may decide when to submit, coalesce, or defer a frame. It
does not know how a panel is driven and must not infer physical completion from
submission acceptance or elapsed time.

The same-build backend source is part of this repository. It owns the
device-specific handles, data formats, power sequencing, and runtime boundary.
The locally extracted waveform and calibration bytes it consumes are not part
of this repository. The public unit tests exercise the scheduler through a
small test engine without simulating real hardware.

Sleep-image decode and durable storage remain outside SurfaceFlinger. The
privileged reference app converts bounded inputs into fixed-size gray and alpha
planes, previews those exact planes, and publishes a generation-checked
catalog. The compositor receives only validated owned buffers. Overlay policy
composes against the latest accepted ordinary full-panel frame and releases
that background on disarm.

Android portrait presentation and the panel-native plane are separate spaces.
The reference transform turns presentation content into panel space once and
inverse-maps final planes for previews. This measured model-level transform is
public configuration, not a device-instance capture.

This division lets another Android 14 e-ink device reuse or replace the policy
without receiving a misleading promise that any device backend will work.

The `profiles/` directory may add model-level public configuration such as
geometry and mode hints. It must never contain a proprietary runtime, private
payload manifest, host identity, or device-instance record. The Neo 2
Android-14 and Android-17 profiles are reference examples; their framework
integrations remain separate.
