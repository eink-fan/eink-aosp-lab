# Color transition fixture

Open `transitions.html` in a browser on a separately authorized test device.
It is self-contained authored HTML, with no network requests, vendor data,
backend controls or automatically running animation. It can also be reviewed
on a desktop, which cannot establish e-ink behavior.

1. Keep lighting, front light, source treatment, waveform and backend cleanup
   settings fixed. Record them outside Git with the selected runtime identity.
2. Select **Black/white origin**, let it settle under the chosen test protocol,
   then select **Target colors**. The page inserts no intermediate white frame.
   Look for prior vertical stripes inside otherwise uniform horizontal rows.
3. Repeat using **Color origin**. The target is identical in both cases.
4. Repeat both transitions with **Independent animation** enabled. Only the
   small square animates; it never requests an explicit backend refresh.
5. Compare cleanup off/on without changing source treatment. Then compare
   Original/Balanced and Vivid off/on as separate experiments. The final row of
   muted colors and neutrals helps expose shading loss or unwanted tint.
6. Stop animation when finished. The page stops it when hidden as well.

Backend-only white/target sequences should be correlated with exact per-phase
progress and bounded optical observations. Do not add browser white between
steps: that would test a different sequence and can hide the native failure.
Check quiet and animated cases repeatedly; one successful quiet trial was not
sufficient in the original investigation. No timing interval in this fixture
is a panel-completion signal.

Record source/tracking, mapper, per-phase wait and total latency separately
from appearance. Do not label accepted calls/s as physical FPS. Screenshots
can verify uniform source rows but cannot prove physical uniformity. See
[the color procedure](../../docs/COLOR_LEARNINGS.md) for verification boundaries.
