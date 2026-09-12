# Neo 2 stock-to-AOSP and system-A updates

Use the Neo 2 (`RM06L`) profile and its stock Android-14 kernel/vendor stack.
The Android-17 image changes the system side; Aura recovery edits, color ABI
and `para` boot-control location do not apply to this device.

## Prepare the stock device

1. Preserve a complete owner-local stock recovery set and hashes before any
   permanent change. Back up shared storage and separately inventory important
   app-private data. They are different backup scopes.
2. Verify unlocked state, USB authorization, root/userdebug access where needed,
   and entry to both bootloader fastboot and userspace fastbootd. Bind the exact
   device serial and fastboot executable; query `is-userspace` on entry.
3. Establish and boot-test stock slot B as the fallback. Require a complete
   non-DSU stock boot and reliable authenticated ADB. Keep B unchanged during
   ordinary A updates. A slot label or an image backup alone is not that proof.
4. Extract the matched Neo runtime with the existing extractor, prepare/build
   the pinned Android-17 profile when authorized, and qualify the candidate
   through the [DSU helpers](../../tools/dsu/README.md).
5. Before first permanent AOSP boot, compare permanent provider schemas and
   provisioning with the target. Include needed data migration or an explicitly
   approved clean userdata transition in the installation plan. Preserve shared
   storage before either; an isolated DSU boot does not migrate permanent data.

The first AOSP transition and later system-only updates have different scopes.
For the first transition, bind top-level `vbmeta_a` explicitly to the preserved
stock-derived image and the selected AVB policy. The established unlocked-device
path uses that verified image with verification/verity disabled for A. This is
an explicit companion-partition operation, not a default part of each update:

```sh
fastboot -s <device> --disable-verity --disable-verification \
  flash vbmeta_a /path/outside/git/verified-stock-vbmeta.img
```

Use only the matching owner-verified image and authorized initial-install
contract. Do not extend this to vendor, boot, the other slot or chained vbmeta
images. Reuse an existing qualified boot/AVB baseline for later system-only
updates when the candidate preserves that compatibility.

## Update an established AOSP slot A

1. Bind the exact raw candidate image, size/hash and qualified DSU. Preserve
   the current physical-A raw image as rollback; verify it by full readback.
2. Read and save the 32-byte boot-control record at offset 2048 in `misc`.
   Require current A, successful A and B, no snapshot update, expected physical
   system size, unchanged recovery/AVB baseline, and compatible permanent data.
3. Enter userspace fastbootd as a separate operation. Recheck the exact serial,
   `is-userspace=yes`, `current-slot=a`, `slot-successful:a=yes`,
   `slot-successful:b=yes`, `snapshot-update-status=none`, and
   `is-logical:system_a=yes`. Verify super capacity and current partition size.
4. Write only `system_a`, using raw build bytes rather than the padded DSU
   package. With verified substitutions, the standard client commands are:

   ```sh
   fastboot -s <device> resize-logical-partition system_a <raw-byte-count>
   fastboot -s <device> flash system_a /path/outside/git/candidate-system.img
   ```

   Record terminal replies and stop on ambiguous failure. Do not automatically
   replay resize/flash or change slot metadata to clear an error.
5. From retained DSU, read the entire physical `/dev/block/mapper/system_a` and
   compare size/hash to the raw candidate. Compare `misc` boot-control bytes
   and boot/AVB baselines independently. A fastboot OKAY is not this readback.
6. Boot physical A twice in separate steps. Verify distinct boot IDs, exact
   system binaries, boot completion, stopped boot animation, non-running DSU,
   enforcing SELinux, stable system_server, expected e-ink services and ADB.
   Check crash buffers, provider/data compatibility, sleep/wake and selected
   front-light behavior. Verify restored files against the owner's manifest.

For an initial one-try AOSP boot from stock B, retain B successful and use a
reviewed device-specific boot-control plan with valid CRC; do not mark A
successful before boot acceptance. The routine update procedure above assumes
an already established successful A and preserves its boot-control record.

The shared [device tooling](../../tools/device/README.md) supplies standard
fastboot/sparse primitives. Its automated writer is Aura-bound; use this
Neo-specific standard-client procedure rather than changing its product guard.
