# Public-release checklist

Before changing this repository from review-only to public open source:

1. Audit every tracked file for independent authorship or a redistributable
   upstream license.
2. Remove vendor names where they unnecessarily imply a private interface.
3. Confirm that no source, patch, comment, test, manifest, hash, or screenshot
   contains personal paths, account or host names, device-instance identifiers,
   private network addresses, build receipts, or proprietary payload details.
4. Confirm that `.gitignore` rejects common firmware, image, binary, and local
   payload forms.
5. Keep `LICENSE`, `LICENSING.md`, and contribution terms aligned with The
   Unlicense; verify that each retained file can be released under it.
6. Run `tools/audit-public-tree.sh` and investigate every match rather than
   weakening the audit to accommodate private material.
7. Run both host test suites from a clean checkout.
8. Review the full staged diff and tracked-file inventory before publishing.
