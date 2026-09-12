# Bring your own vendor inputs

This repository provides authored source and owner-operated instructions.
It does not provide or download proprietary device software. Recovery and
installation procedures consume the owner's locally verified images; they
do not grant redistribution rights to those inputs.

If you own a compatible device and are permitted by the applicable software
terms to inspect its local software, use a matching OEM-provided stock image
or a locally accessible copy from your own device. Keep the source and all
extracted material outside Git.

## Local-only staging

For the Neo 2 Android-17 profile, use the checked-in owner-operated ADB
extractor. It writes a new local payload directory, a source-identity record,
an integrity lock, and a validated front-light calibration generated from the
stock system's active resource arrays. It never uploads or commits those bytes:

```sh
tools/extract-neo2-android17-vendor.sh --output /path/outside/git/neo2-payload
```

The checked-in [`frontlight-model.env`](../profiles/neo2-android17/frontlight-model.env)
contains the model-level endpoint names and neutral, bounded power defaults.
Copy it outside Git and pass `--frontlight-model` to the extractor if you want
to tune those local values. The generated calibration remains editable in the
local payload and is validated again during setup.

For an unsupported device, create an untracked manifest from
`local.example/vendor-files.local.example`. The manifest is a private list of
relative paths selected by the device owner; do not commit it or paste it into
an issue. Then run:

```sh
tools/extract-local.sh \
  --source /absolute/path/to/your/local/stock-tree \
  --manifest /absolute/path/to/your/vendor-files.local \
  --output /absolute/path/to/a/new/local-payload
```

The helper only copies regular files from the supplied local source tree,
refuses an existing output directory, and writes a local integrity lock. It
does not contact a device or network service. A same-build backend may consume
that ignored payload only after validating the generated lock.

Never put a payload, stock image, logs, a device-specific fingerprint, or a
private extraction lock in this repository. A reviewed profile may contain a
hash lock for specifically named vendor inputs; a hash identifies expected
bytes but does not redistribute them.
