# Safety

- Preserve a verified owner-local recovery baseline before modifying a device.
- Unlocking, boot-image changes, userdata erasure and permanent installation
  are explicit owner decisions. Bind each operation to the exact device,
  image hash, partition and recovery plan; do not chain it from a build.
- Prefer read-only evidence and reversible experiments.
- Do not collect or publish personal data, logs, account material, keys, or
  device identifiers.
- Treat queue acceptance and timing as submission evidence only, never proof
  that an e-ink panel completed a physical update.
