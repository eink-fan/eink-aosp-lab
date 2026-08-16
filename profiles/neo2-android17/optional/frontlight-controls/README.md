# Optional front-light control surfaces

This independently authored patch bundle proposes the public AOSP integration
for the manager-owned Neo 2 front-light controls. It is opt-in so the existing
conservative profile remains byte-for-byte unchanged unless a builder selects
it explicitly:

```sh
tools/setup-neo2-android17.sh \
  --workspace /path/to/new/neo2-aosp17 \
  --payload /path/outside/git/neo2-payload \
  --with-frontlight-controls
```

The bundle adds:

- a system-server desired-state manager with display-off zeroing and ordered
  wake restoration;
- Settings, SystemUI dialog, expanded Quick Settings, and Quick Settings tile
  surfaces backed by that manager;
- brightness-only requests that preserve manager-owned warmth;
- reference-app packaging and the Binder service policy needed by the control
  chain; and
- focused Settings and SystemUI source tests.

The bundle intentionally does **not** commit extracted vendor data or
device-instance records. Instead, the owner-operated extractor reads the active
stock resource arrays and generates an editable local calibration. This option
grants SurfaceFlinger `{ getattr open write }` only on the exact model-typed
cool and warm primary nodes; it grants no generic LED or sysfs access.

With a matching owner-local payload, the bundle is intended to provide
functional front-light control. Manager state is still desired state rather
than hardware readback, and a build or accepted request does not prove physical
application. The backend rejects malformed calibration and inaccessible
endpoints, caps combined power, zeros both channels for display-off, and
latches the first write failure until reboot.

These patches target only the exact AOSP revisions pinned by the parent
Android-17 profile and apply after its base patches. They stop before a newer
experimental warmth-slider revision that has not completed the same
qualification boundary.

Reader preferences such as timeouts, font scale, setup completion, and default
applications are independent product choices rather than private data. They
remain outside this front-light bundle so builders can select them separately;
application binaries still must be supplied under their own redistribution
terms.
