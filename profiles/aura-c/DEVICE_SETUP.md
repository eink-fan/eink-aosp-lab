# Aura C device setup and installation

Use this with the [Android-14 integration](ANDROID14.md). Every image, serial,
hash and size below comes from the owner's current device and local build.
Keep those values and receipts outside Git. Preparing instructions or an image
does not perform or authorize the next device operation.

## 1. Establish access and recovery

1. Back up personal data and retain the device's original boot image and a
   verified stock recovery set. Record image sizes and SHA-256 values locally.
2. Enable USB debugging, authorize the host and identify the connected product:
   `ro.product.device` must be `ATILIM_mPAD07`. Record the running API level;
   it determines which DSU input descriptor to use.
3. Verify bootloader `unlocked=yes` before boot-image or system writes. The
   established Aura procedure starts at this state. No Aura-specific unlock
   command is established by the available source records. If it is locked,
   obtain a device-matched unlocking procedure before proceeding; do not reuse
   Ocean or Musnap X confirmation/button instructions.
4. Keep authorized root access available for stock runtime extraction and DSU.
   A stock-only Magisk module may restore `adb_enabled=1` after boot: gate it on
   product `ATILIM_mPAD07` and SDK 30, check every five seconds after
   `sys.boot_completed=1`, and stop when its disable/remove flag appears.
   Preserve ADB authentication and wireless-debugging settings. Use native
   userdebug ADB on the Android-14 system.

If installing Magisk for this stock-only path, patch the device's own preserved
boot image on that device with the owner-selected official release. Verify the
patched image, partition size and original backup before a separately approved
`boot_a` write. Verify full boot readback afterward. No patched image or APK is
distributed here.

## 2. Use the correct fastboot transport

On the tested Mac path use standard fastboot packets through PyUSB/libusb.
Select the device by its private per-mode serial and the USB interface tuple
class/subclass/protocol `ff/42/03`. Claim that interface and its bulk IN/OUT
endpoints. ADB uses `ff/42/01`; VID/PID alone is insufficient.

For each command, write its ASCII fastboot packet and read replies. Consume
bounded `INFO` replies until a terminal status. Parse the first four bytes:
`OKAY` may include trailing text; `FAIL` is failure; `DATA` announces the exact
download byte count. Respect `max-download-size`, check every byte transfer,
and stop on timeout, short transfer or unknown status. Release the interface
after the operation. No download-agent or boot-ROM exploit is required by
this transport.

Query `product`, `unlocked`, `current-slot`, `is-userspace` and partition sizes
before writing. Bootloader must report `is-userspace=no`; recovery fastbootd
must report `yes`. Bind their serials independently.

## 3. Prepare direct recovery fastbootd

Use the owner's matching boot-v2 image with recovery ramdisk. The studied boot
partition is 32 MiB; verify the actual size rather than assuming it. Unpack
the ramdisk with matching boot tools, preserving entry ownership, modes and
all unrelated image components.

In both `init.recovery.mt6771.rc` and `init.recovery.mt8788.rc`, select cable
mode 1 for the existing UDC:

```text
write /sys/class/udc/musb-hdrc/device/cmode 1
```

In recovery `system/etc/init/hw/init.rc`, add `disabled` to the ordinary
`service recovery /system/bin/recovery` stanza and add:

```text
on boot
    setprop sys.usb.controller musb-hdrc
    write /sys/class/udc/musb-hdrc/device/cmode 1
    setprop sys.usb.config fastboot
```

Use the existing fastbootd service/USB configuration. Repack with the matching
compression and boot-header tooling; verify ramdisk round-trip contents,
header/component integrity, image size and hash. Preserve the original image.
This configuration starts fastbootd instead of the ordinary recovery UI;
conventional recovery-menu or recovery `--wipe_data` instructions do not apply.

Install only the verified recovery-capable `boot_a` image under its own approved
operation. Confirm its full readback. Enter it with `adb reboot recovery`, then
wait for USB enumeration and verify `is-userspace=yes` and logical partitions
before beginning another operation.

## 4. Install a temporary DSU

Package the candidate system with the selected target's AVB/FEC settings.
Record packaged size and SHA-256 separately from the raw system image. Copy
the full package to a device-local regular file and verify that hash again.
Set that file only to root ownership, mode 0600 and SELinux context
`u:object_r:gsi_data_file:s0`. Keep SELinux enforcing.

Inspect `gsi_tool status`; installation requires non-running DSU state and
sufficient free storage. Removing an existing DSU is a separate deliberate
operation. Use the command form for the currently running host, in its
authorized root shell, with verified values substituted:

**Stock Android 11/API 30 host:**

```sh
gsi_tool install --gsi-size <packaged-byte-count> --no-reboot 1< <staged-image>
```

**Integrated Android 14/API 34 host:**

```sh
gsi_tool install --gsi-size <packaged-byte-count> \
  --userdata-size 8589934592 --no-reboot 0< <staged-image>
```

The angle-bracket names are substitution notation, not literal shell arguments.
Use a regular staged file and the specified descriptor; do not replace this
path with a pipe. Wait for terminal installation success, then run
`gsi_tool disable` and inspect status for installed/disabled state and the
expected AVB key. Separately enable one boot with `gsi_tool enable -s`.
Reboot only when ready to leave the current session.

## 5. Verify the guest

- Confirm DSU-running state, boot completion, API level, enforcing SELinux,
  logical geometry and exact hashes of changed system binaries.
- Verify the mapped `system_gsi` image when doing full package readback;
  ordinary backing-file views are not interchangeable with that mapping.
- Confirm DeviceLock/SystemUI services and Bluetooth ON, then exercise the
  Bluetooth operations required by the intended use. Inspect crash buffers.
- Exercise Controls, sleep-image selection, screen-off and wake. Observe all
  four light channels while the framework is Asleep, then again Awake.
- Run the [color fixture](../../tools/color/README.md) with quiet and animated
  traffic, checking native white/target progress and physical appearance.
- Verify sleep catalog delivery after app replacement and reboot. Treat guest
  userdata as isolated from the permanent installation.

## 6. Prepare permanent userdata and rollback

Return to the physical system and capture its SettingsProvider schema,
provisioning/setup flags, application-provider compatibility and desired
light/sleep settings. A successful isolated DSU boot does not validate these
permanent databases. Preserve a known-good raw system image for rollback and
verify the current physical system against it.

For a clean installation, back up internal shared storage and app data before
an explicitly approved userdata erase. The matched recovery fastbootd path
erases the verified `userdata` partition; stock vendor fstab supplies its
formattable ext4 initialization. Do not include metadata, external SD or other
partitions in that operation. Inspect and separately clean residual DSU
registration before later DSU reuse.

Record the current boot-control record read-only: on this Aura layout it is
32 bytes at offset 2048 in `para`. Retain it as a comparison baseline, including
reserved bytes and CRC. Do not rewrite boot control to make it match an older
record or select another slot as part of system installation.

## 7. Write only the selected logical system partition

The established permanent path writes `system_a` in recovery fastbootd. Require
all of the following immediately before mutation:

```text
product=ATILIM_mPAD07       is-userspace=yes
unlocked=yes               current-slot=a
slot-successful:a=yes      slot-unbootable:a=no
snapshot-update-status=none is-logical:system_a=yes
```

Verify current system/super geometry, available logical space, raw candidate
size/hash, qualified DSU identity, recovery boot hash and rollback image. Create
a new owner-local operation record before the first mutation.

Issue `resize-logical-partition:system_a:<raw-byte-count>`, verify the new size,
then download and flash sparse regions with standard `download:<hex-size>` and
`flash:system_a` packets. The established region size is 32 MiB, subject to the
reported download limit. Each sparse region represents its absolute location
in the full image: encode zeros as FILL and regions outside the chunk as
DONT_CARE. Test the sparse encoder independently before use. Record each
region's offset, raw length, hash and terminal reply.

Do not write boot, vendor, vbmeta, the other slot, userdata or boot control
during this system-only operation. After an ambiguous partial write, stop and
inspect rather than replaying the operation automatically.

## 8. Verify permanent installation

Independently read the complete physical `system_a` through the retained DSU
and compare its length/hash with the raw candidate. Then verify two distinct
physical-A boots: exact changed binaries, enforcing SELinux, provisioning and
settings, expected services, Bluetooth state and crash buffers. Compare boot
image and boot-control bytes with the pre-write baseline. Recheck sleep/wake
and desired light state on permanent userdata. Keep the verified rollback
image and recovery material outside Git.
