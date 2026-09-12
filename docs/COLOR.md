# Color presentation

Follow the [color implementation procedure](COLOR_LEARNINGS.md) for RGB
tracking, treatment order, paired-plane mapping, exact marker guards and tests.

Color capability, Android framework version, and device/runtime identity are
independent choices. A monochrome panel may consume RGB composition; a color
panel may show grayscale content. Neither buffer format establishes panel
capability. Android 14 does not imply color, and color does not imply Aura C.

## Portable source treatment

The host-built core provides `ApplyGrayPreference` and `ApplyVividColor` in
`eink_gray_preference.h`. Both operate on packed RGBA8 source pixels, preserve
alpha, and leave malformed byte counts unchanged. Neutral preference suppresses
small RGB tints using a smooth transition; Vivid reduces the shared RGB
component while preserving the strongest channel. These are code-space visual
preferences, not color management, measured calibration, or waveform selection.
Use resolved, straight-alpha source pixels; flatten transparent composition
before a backend that requires opaque input. Do not apply them to vendor-mapped
output. Original treatment and Vivid off are the neutral starting choices.

`ColorQualityHistory` analyzes accepted RGBA targets and optionally prepares a
selective white intermediate for changed chromatic pixels. `Prepare` does not
advance history; call `Accept` only after the target is accepted by the engine.
A dropped or rejected candidate must not become the comparison baseline.
Input and intermediate storage must not alias. This utility is serial-owner
policy and allocates a full target-sized buffer when needed; callers must bound
geometry and storage before calling it. Reset history on geometry, orientation,
runtime, or treatment changes, even if the new byte count is identical.

An intermediate is a proposed extra submission, never permission to drive a
panel. A backend must explicitly support the sequence, retain the target across
the intermediate, and handle target rejection without treating the intermediate
as a completed target. Disable this policy unless that sequence is qualified.
Queue history is not physical panel history. Neither timestamps nor an accepted
call prove the intermediate has physically completed.

These utilities are compiled and tested in `einklab_core`. They are opt-in
reference components; the pinned Neo 2 Android graft does not automatically
consume them. Tests cover threshold behavior, malformed input, alpha, accepted
history, discarded candidates, and scalar/bulk agreement.

## Backend responsibilities

`RgbFrameTracker` provides bounded, exact RGB capture signatures for the
existing demand gate. `CheckEngineMarkerStart` provides opt-in exact-marker
progress decisions for a verified backend contract. Neither is automatically
wired into the pinned Android graft. See [the procedure](COLOR_LEARNINGS.md)
before integrating them, especially the distinct capture/accepted histories
and the protection required for both phases of a cleanup pair.

The existing `Frame::source_buffer` / `ComposeBuffer` seam can carry an owned
RGB source or a backend-defined set of planes without changing the scheduler.
The optional `grayscale_buffer` is not a universal engine input requirement.
Declare and validate source format, stride, orientation, bounds, plane pairing,
and lifetime in the selected backend. Retain data beyond `Submit` if the runtime
continues reading it asynchronously.

Preserve RGB through capture, ordinary frames, sleep images, and previews when
color is selected. A gray-only catalog cannot recover discarded chroma. Compare
RGB changes as well as gray changes: equal-luma color transitions must not be
discarded by a grayscale delta gate. Color treatment, refresh mode, cleanup,
and submission pacing must remain separately selectable policies.

Some runtimes require mapped color plus a companion gray plane derived from
the same source generation. Others do not. Color-filter-array mapping, lookup
tables, waveforms, and initialization order belong to the matched backend and
owner-local payload. See the [Aura C reference](../profiles/aura-c/README.md)
for one concrete contract, not a universal color implementation.

Screenshots validate composed pixels. Optical color, ghosting, and refresh
quality require separately supervised physical observation.
