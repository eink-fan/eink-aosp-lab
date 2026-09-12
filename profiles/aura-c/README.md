# Aura C model reference

Start with [Android-14 integration](ANDROID14.md), then follow
[device setup and installation](DEVICE_SETUP.md). The current
[Ink Controls source](../../android/ink-controls/README.md) is included separately
from the older app bound to the Neo 2 recipe.

This is a model/runtime contract extracted from authored integration work. It
contains no vendor implementation, lookup tables, calibration, operational
history, or device-instance records. It is not a standalone build or install
profile. The public Neo 2 extractor and setup scripts must not be used for it.

## Geometry and runtime

Follow the [color procedure](../../docs/COLOR_LEARNINGS.md) for source treatment,
paired-plane conversion and marker-protected cleanup. Select the progress
contract explicitly for this runtime.

- Logical portrait composition: 1860 × 2480; panel-facing input: 2480 × 1860.
- The observed legacy composer anomaly is exactly 350 × 1907. Correction
  belongs only in a selected Aura C binding; do not repair arbitrary modes.
  Correct geometry passes unchanged and unknown geometry must fail closed.
- The studied color backend consumes mapped RGBA plus a companion gray plane.
  Both must come from the same owned source generation and orientation.
- The studied mapping sequence initializes CFA, AIE, VPCOA and engine
  color/regal support, then uses VPCOA mapping type 1 with increment 8 and
  derives the T1000 gray companion from that mapped output. These are observed
  runtime-specific settings, not portable defaults. The owner-local table
  storage must survive initialization and later asynchronous mapper reads.
- The studied submitted/display-start counters support the paired-refresh
  guard only under sole-writer ownership. The runtime reserves the maximum
  32-bit counter value; its last trackable marker is therefore maximum minus
  one. Its five-second observation ceiling and two-millisecond polling were
  bounded failure handling, not calibration, fixed dwell or completion proof.
- Matching owner-local color-filter-array, image-enhancement, and color-mapping
  tables and the matching gray conversion routine are required. Generic
  software luma is not established as a substitute for that companion plane.
- Initialization and mapping order, runtime ABI, accepted refresh hints, and
  buffer lifetime must be verified together. Neo 2 numeric hints are not an
  Aura C mode table.

Android 14 is one studied framework target for this model, independently of
its older stock composer/runtime origin. This does not establish compatibility
with every Android-14 image or with Android 17. See
[framework integration](../../docs/ANDROID_INTEGRATION.md).

The [portable color policies](../../docs/COLOR.md) contain no Aura conditionals.
They may be useful on other validated color backends. Conversely, this model's
geometry and runtime validation still apply when displaying monochrome content.
Front-light channel topology and calibration are another independent hardware
contract; Neo 2's two-node permission bundle does not apply here.

To turn this reference into a reproducible public build profile, contribute an
immutable source manifest, a reviewed target-specific patch series, owner-only
extraction and identity validation, and bounded backend tests. Keep vendor
bytes outside Git under [BYO_VENDOR](../../docs/BYO_VENDOR.md). No device
qualification is claimed by this reference or the portable host tests.
