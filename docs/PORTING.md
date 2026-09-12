# Porting to another e-ink Android device

1. Start with a stock-recovery plan and read-only inspection. Do not assume an
   Android 14 userspace implies compatible display hardware.
2. Build and run the public host suite before writing a device backend.
   Select the [framework target](ANDROID_INTEGRATION.md), model/runtime and
   [color policies](COLOR.md) independently. Neither Android version nor panel
   color capability establishes runtime compatibility.
3. In a private workspace, write a backend that implements only the public
   `Engine` contract and owns its device-specific dependencies locally.
4. Validate buffer ownership, geometry, and failure behavior before attempting
   a panel update. Submission acceptance is not physical-completion evidence.
5. Prefer reversible system-image/DSU experiments where the device supports
   them. Each install, boot, interaction, and cleanup step needs its own
   deliberate recovery check.
6. Keep results factual: report the Android version, test scope, and observed
   behavior; do not generalize one device's backend to another.

Root or ADB access may simplify local investigation, but it does not grant a
right to redistribute proprietary files or authorize irreversible changes.
