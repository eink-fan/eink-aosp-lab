# Public-release checklist

Before changing this repository from review-only to public open source:

1. Audit every tracked file for independent authorship or a redistributable
   upstream license.
2. Remove vendor names where they unnecessarily imply a private interface.
3. Confirm that no source, patch, comment, test, manifest, hash, or screenshot
   names proprietary payloads or private paths.
4. Confirm that `.gitignore` rejects common firmware, image, binary, and local
   payload forms.
5. Keep `LICENSE`, `LICENSING.md`, and contribution terms aligned with The
   Unlicense; verify that each retained file can be released under it.
6. Run the host test suite from a clean checkout.
7. Review the full staged diff before publishing.
