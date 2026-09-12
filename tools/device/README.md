# Owner-operated device tools

These tools contain authored implementation, no images or private device
values. They were added under a static-inspection-only constraint and have not
been executed or qualified in this public assembly. Review the exact contract
and selected device before authorizing a mutation. No tool automatically
chains recovery, installation, reboot and qualification.

## Prepare recovery locally

Use the matching owner-patched boot-v2 image and an owner-supplied host
`magiskboot` executable:

```sh
python3 tools/device/prepare-aura-recovery.py \
  --source /path/outside/git/owner-patched-boot.img --sha256 <source-sha256> \
  --magiskboot /path/to/magiskboot \
  --output /path/outside/git/aura-direct-fastbootd.img
```

The helper changes only the two cable-mode entries and recovery init startup,
round-trips the compressed archive, retains original component boundaries and
updates the boot image ID. It neither installs the image nor contacts a device.
The resulting recovery starts fastbootd in place of the ordinary recovery UI.
Follow [Aura device setup](../../profiles/aura-c/DEVICE_SETUP.md) for the separate
boot-image operation and recovery entry.

## Temporary DSU

For each command, omit `--execute` to inspect its local plan. Substitute the
AVB/FEC package hash/size, not the raw-system identity:

```sh
python3 tools/device/aura-dsu.py stage --serial <device> \
  --image /path/outside/git/aura-dsu/system-aura-android14-avb.img \
  --sha256 <package-sha256> --size <package-bytes> --execute

python3 tools/device/aura-dsu.py install --serial <device> \
  --sha256 <package-sha256> --size <package-bytes> --execute

python3 tools/device/aura-dsu.py enable --serial <device> \
  --sha256 <package-sha256> --size <package-bytes> --execute
```

Staging requires a new fixed device directory; installation requires clean
normal DSU state. The installer selects descriptor 1 on stock API 30 and
descriptor 0 with 8-GiB isolated userdata on API 34. It finishes disabled.
The separate enable operation arms one boot only. Reboot is manual. The helper
does not wipe an existing DSU or clean staged files; those remain separate
owner decisions. Inspect terminal status and the expected AVB key before boot.

## Capture the permanent baseline

With authorized root ADB and the physical system active:

```sh
python3 tools/device/aura-baseline.py --serial <device> --physical-boot \
  --output /path/outside/git/baseline.json
```

This is read-only. It records the complete physical-system hash/size, recovery
boot hash, boot-control bytes, boot identity, provisioning and service state.
Preserve the matching raw rollback image. Finish separate DSU and optical
qualification before preparing a permanent-write contract.

## Plan system-A promotion

Create the following JSON outside Git using current verified values. Numeric
sizes are JSON integers. `dsu_qualified_sha256` is the owner's attestation
binding completed qualification to the raw candidate, not an automatic test:

```json
{
  "schema": 1,
  "product": "ATILIM_mPAD07",
  "fastbootd_serial": "OWNER_LOCAL_VALUE",
  "image": "/path/outside/git/candidate-system.img",
  "image_sha256": "RAW_CANDIDATE_SHA256",
  "image_size": 0,
  "dsu_qualified_sha256": "RAW_CANDIDATE_SHA256",
  "rollback_image": "/path/outside/git/rollback-system.img",
  "rollback_sha256": "ROLLBACK_SHA256",
  "baseline": "/path/outside/git/baseline.json",
  "baseline_sha256": "BASELINE_JSON_SHA256",
  "boot_a_sha256": "BASELINE_BOOT_SHA256",
  "boot_control_hex": "BASELINE_BOOT_CONTROL_HEX",
  "system_a_size": 0,
  "super_size": 0
}
```

Zero sizes and placeholder hashes are invalid and must be replaced. Bind the
recovery fastbootd serial separately from normal ADB. Run the local plan:

```sh
python3 tools/device/promote-system-a.py \
  --contract /path/outside/git/promotion.json \
  --receipt /path/outside/git/new-promotion-receipt
```

After separately entering verified recovery fastbootd, an explicitly approved
write uses the same command with `--write-system-a`. Install PyUSB/libusb on
the owner host; `--libusb /path/to/library` selects a nonstandard library path.

The writer checks serial, model, userspace mode, unlocked state, successful
active A, snapshot state, logical geometry, candidate and rollback hashes. It
writes only `system_a`, using the standard sparse encoder. Its exclusive
`WRITE.started` claim and per-region receipts are retained on failure. It does
not reboot. `ACCEPTED.txt` means the transport accepted all regions, not that
installation is verified. Never relabel or replay an uncertain partial write.

## Verify the result separately

From retained DSU, read the full physical partition and compare baseline:

```sh
python3 tools/device/aura-baseline.py --serial <device> \
  --verify-baseline /path/outside/git/baseline.json \
  --candidate-sha256 <raw-candidate-sha256> --candidate-size <raw-bytes> \
  --output /path/outside/git/readback.json
```

Then boot physical A twice, separately, and repeat with `--physical-boot` and
distinct output filenames. Verify distinct `boot_id` values and the feature
checks in the device guide, including services, Bluetooth, crash buffers and
sleep/wake. Baseline hash/provisioning checks do not replace those checks.

## Other devices and unlocking

Sparse encoding and fastboot packet handling are generic. Aura partition
identity, boot layout, calibration and recovery edits are not. A Neo 2 writer
needs its own reviewed model/partition/recovery contract; do not change the
Aura product string to bypass it. Use the existing Neo 2 DSU helpers for its
temporary-image path.

The available Aura source records establish the transport/recovery procedure
on an unlocked bootloader. They do not establish a device-specific unlock
transaction or confirmation method. Verify `unlocked=yes` as a prerequisite;
do not substitute another model's unlocking procedure.
