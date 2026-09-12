# Device e-ink integration source

This authored native snapshot supplies device profiles, grayscale and color
presentation, RGB demand tracking, engine-marker coordination, front-light and
sleep-image integration. The [Aura Android-14 recipe](../../profiles/aura-c/BUILD.md)
stages it under SurfaceFlinger together with current Ink Controls and the
ordered framework patches. Existing module and namespace names are retained.
Vendor runtime bytes, waveform tables and calibration remain owner-local.

Neo 2 and Aura C profiles enable presentation and continuous submission in all
build variants. There is no lifetime update cap during normal operation; pacing,
demand gating and ownership checks still apply. New model definitions default
to disabled presentation and finite submission. Color capability and Android
version do not determine qualification. Volatile `debug.neo2.eink.enabled=0`
selects capture-only diagnosis; `debug.neo2.eink.continuous=0` selects the finite
budget (12 by default, configurable up to 24 calls).

The pinned Neo 2 Android-17 recipe uses `android/neo2-eink`; do not swap source
grafts without their matching framework integration. This new public Aura
assembly has not been built or device-tested. See [feature coverage](../../docs/FEATURES.md).
