# Implementing the color pipeline

Keep framework version, model/runtime binding and color capability independent.
Use the portable core for source policy and the selected backend for mapping,
waveforms, transport and progress. The current public utilities are opt-in;
wire them into the matching Android integration before enabling them.

## 1. Capture and track RGB

Capture an owned, synchronized composition snapshot. Validate layout, geometry,
stride and available bytes; copy only visible pixels. Transform presentation
into panel space once. Use `RgbFrameTracker(max_pixels)` with the readable
byte count to track exact RGB changes in the existing demand gate. It handles
RGBA/BGRA, row padding and geometry changes without retaining borrowed memory.

Use its alpha-ignoring comparison only on resolved composition. Preserve
same-luma color transitions through downstream idle and delta gates. Treat its
signature as a generation for consecutive equal captures, not a global content
hash or evidence that a target was accepted. Explicit refresh bypasses ordinary
unchanged-content suppression.

## 2. Apply source treatment once

Start with a fresh owned source copy. Apply `ApplyGrayPreference`, then optional
`ApplyVividColor`, then transition analysis. Map only after those operations.
Never run source treatment on vendor-mapped output or repeatedly on a treated
history buffer.

| Policy | Source behavior |
| --- | --- |
| Original | Preserve source RGB |
| Balanced | Smoothly neutralize RGB spreads between 8 and 24 |
| Stronger | Smoothly neutralize RGB spreads between 16 and 40 |
| Vivid | Above spread 24, reduce shared RGB while preserving the strongest channel; full strength at spread 64 removes up to one quarter of the minimum channel |

Both treatments preserve alpha. Vivid preserves neutrals and already saturated
pixels; check muted-color shading when selecting it. These are code-space
preferences, not calibrated color management. Reset transition history and
request a redraw when treatment changes, even if source content is unchanged.
Keep waveform and cleanup settings fixed during treatment comparisons.

## 3. Prepare accepted-target cleanup

Keep `ColorQualityHistory` on the serial backend owner. Call `Prepare` with
bounded packed RGBA and nonaliasing output storage. Default selective cleanup
qualifies target pixels when channel spread is at least 24, maximum RGB change
is at least 12, and the accepted origin's minimum channel is below 232.

The intermediate uses white at qualifying pixels and target content elsewhere.
Explicit cleanup can select all chromatic targets without prior history.
Full-white-then-target, selective cleanup and a full-waveform refresh are
separate operations. Do not encode them as interchangeable mode hints.

Advance history only after target acceptance. First accepted content seeds it.
Reset on geometry, orientation, treatment, runtime and sleep-session changes,
even at equal buffer size. Dropped captures and rejected candidates do not
advance accepted-target history. Stop and invalidate history after an uncertain
partially submitted pair.

## 4. Map paired planes

Bind the exact owner-local runtime and tables. Validate initialization and
complete writes to output planes. For the Aura contract, initialize CFA, AIE,
VPCOA and engine color/regal support, retain table storage for the entire
backend lifetime, and derive T1000 companion gray from mapped color.

Each phase needs color and gray from the same generation, orientation and
source treatment. Generic pre-mapping luma is not that companion. Keep source,
tables, mapped buffers and submitted data alive according to their distinct
runtime lifetimes. Reuse mapped buffers only when the runtime's copy semantics
permit it. A display-start marker does not release arbitrary borrowed buffers.

## 5. Protect both phases at the backend queue

Enable paired cleanup only with a verified non-coalescing progress contract.
Reserve both submission slots before starting. On one serial owner:

```text
submit white -> observe exact white start -> submit target
             -> observe exact target start -> release following input
```

Keep incoming ordinary frames eligible to coalesce upstream. Map the target
while white progresses if verified buffer ownership permits it. Ordinary
single-phase frames retain their existing cadence.

Use `CheckEngineMarkerStart` only when the backend establishes a synchronous
single counter increment per call, sole submission ownership, and an exact
start marker beyond the mergeable pending queue. Supply the binding's last
usable marker and finite observation deadline. Reject skipped markers,
unexpected writers, reset/rollover and timeout. Do not accept a later marker
as proof the expected intermediate survived. Disable paired cleanup on a
backend without equivalent verified progress semantics.

The helper is a predicate: the backend owns polling, timeout handling and
poisoning. Fixed delays are pacing controls, not evidence of queue progress.
Start, complete waveform emission, buffer release and optical completion are
separate boundaries. Keep all four explicit in diagnostics.

## 6. Preserve color through sleep and preview

Keep owned RGB through sleep-image decode, catalog publication and preview.
Apply the same source treatment and panel transform as ordinary frames. A
same-size gray catalog cannot recover discarded chroma. Expanding gray into
equal RGB is a monochrome fallback only.

Use generation-checked delivery and rebind the provider after package replacement.
Order the final sleep frame before power-off according to the backend contract.
Label previews as source or mapped output; neither proves physical appearance.

## 7. Verify output and performance

Run the [transition fixture](../tools/color/README.md): black/white origins to
uniform target rows, then color origins to the same target. Repeat with
independent animation. The browser must not insert its own white phase.
Compare cleanup separately from neutral preference and Vivid. Observe settled
physical uniformity, residual stripes, shading and latency under fixed lighting.

Record accepted targets and extra intermediate calls separately. Measure
capture/tracking, treatment, mapping, white-start wait, target-start wait and
whole-pair latency. Correlate exact per-phase markers with waveform execution.
Repeat with verbose tracing off. Treat cached counters and stale last-pair
fields according to their update interval; do not label submission rate as
physical FPS.

| Check | Acceptance boundary |
| --- | --- |
| Host policy tests | Exact RGB, thresholds, malformed input, alpha, ownership and marker decisions |
| Target compilation | Actual Android platform types, signed span sizes and ABI |
| Conversion probe | Complete writes and matching paired-plane contract |
| Composed screenshots | Correct app/compositor pixels |
| Per-phase markers | Exact phase reached the verified protected stage |
| Waveform evidence | Runtime executed the phase |
| Optical comparison | Physical appearance in the recorded conditions |

Compile new portable units with actual target flags as well as the Android
bridge. Verify vector blocks against scalar behavior, including row tails and
channel-layout changes. Keep vendor bytes and device observations outside Git.
