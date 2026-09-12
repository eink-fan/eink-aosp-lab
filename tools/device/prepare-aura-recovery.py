#!/usr/bin/env python3
"""Prepare an owner-local boot-v2 direct-fastbootd image; no device access."""
import argparse
import gzip
import hashlib
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', required=True, help='matching owner-patched boot-v2 image')
    p.add_argument('--sha256', required=True)
    p.add_argument('--output', required=True)
    p.add_argument('--magiskboot', required=True, help='owner-supplied host executable')
    args = p.parse_args()
    source_path, output = Path(args.source).resolve(), Path(args.output).resolve()
    root = Path(__file__).resolve().parents[2]
    require(not output.exists() and root not in output.parents and output != root,
            'output must be new and outside Git')
    require(0 < source_path.stat().st_size <= 64 * 1024 * 1024, 'invalid input size')
    source = source_path.read_bytes()
    require(hashlib.sha256(source).hexdigest() == args.sha256, 'input hash mismatch')
    require(source[:8] == b'ANDROID!' and len(source) >= 1660, 'not an Android boot image')
    require(struct.unpack_from('<I', source, 40)[0] == 2, 'requires boot header v2')
    page = struct.unpack_from('<I', source, 36)[0]
    require(page in (2048, 4096, 8192, 16384), 'unsupported boot page size')
    align = lambda n: ((n + page - 1) // page) * page
    kernel = struct.unpack_from('<I', source, 8)[0]
    ramdisk_size = struct.unpack_from('<I', source, 16)[0]
    offset, capacity = page + align(kernel), align(ramdisk_size)
    require(ramdisk_size > 0 and offset + capacity <= len(source), 'invalid ramdisk bounds')
    def image_id(image):
        digest, position = hashlib.sha1(), page
        for field in (8, 16, 24, 1632, 1648):
            size = struct.unpack_from('<I', image, field)[0]
            require(position + size <= len(image), 'component exceeds image')
            digest.update(image[position:position + size])
            digest.update(struct.pack('<I', size))
            position += align(size)
        return digest.digest()
    require(image_id(source) == source[576:596], 'source component ID mismatch')
    inflater = zlib.decompressobj(31)
    archive = inflater.decompress(source[offset:offset + ramdisk_size], 64 * 1024 * 1024 + 1)
    require(inflater.eof and len(archive) <= 64 * 1024 * 1024, 'invalid or oversized gzip ramdisk')
    position, rebuilt, changed = 0, bytearray(), set()
    cable_files = {'init.recovery.mt6771.rc', 'init.recovery.mt8788.rc'}
    trailer = False
    while position + 110 <= len(archive):
        header = bytearray(archive[position:position + 110])
        require(header[:6] == b'070701', 'requires newc archive')
        namesize, size = int(header[94:102], 16), int(header[54:62], 16)
        require(1 <= namesize <= 4096, 'invalid archive name')
        name_bytes = archive[position + 110:position + 110 + namesize]
        require(len(name_bytes) == namesize and name_bytes[-1] == 0, 'invalid name bounds')
        name = name_bytes[:-1].decode()
        data_offset = (position + 110 + namesize + 3) & ~3
        require(data_offset + size <= len(archive), 'invalid archive entry bounds')
        data = archive[data_offset:data_offset + size]
        position = (data_offset + size + 3) & ~3
        if name in cable_files:
            before = b'write /sys/class/udc/musb-hdrc/device/cmode 2'
            require(data.count(before) == 1 and name not in changed, 'cable-mode preimage differs')
            data = data.replace(before, b'write /sys/class/udc/musb-hdrc/device/cmode 1')
            changed.add(name)
        elif name == 'system/etc/init/hw/init.rc':
            before = b'service recovery /system/bin/recovery\n'
            require(data.count(before) == 1 and name not in changed and
                    before + b'    disabled\n' not in data, 'recovery preimage differs')
            data = data.replace(before, before + b'    disabled\n')
            data += (b'\non boot\n    setprop sys.usb.controller musb-hdrc\n'
                     b'    write /sys/class/udc/musb-hdrc/device/cmode 1\n'
                     b'    setprop sys.usb.config fastboot\n')
            changed.add(name)
        header[54:62] = f'{len(data):08x}'.encode()
        rebuilt.extend(header + name_bytes)
        rebuilt.extend(bytes(-len(rebuilt) % 4))
        rebuilt.extend(data)
        rebuilt.extend(bytes(-len(rebuilt) % 4))
        if name == 'TRAILER!!!':
            trailer = True
            break
    require(trailer and changed == cable_files | {'system/etc/init/hw/init.rc'},
            'required recovery entries unavailable')
    rebuilt.extend(bytes(-len(rebuilt) % 512))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='aura-recovery-', dir=output.parent) as temporary:
        raw, compressed_path = Path(temporary) / 'ramdisk.cpio', Path(temporary) / 'ramdisk.gz'
        raw.write_bytes(rebuilt)
        subprocess.run([str(Path(args.magiskboot).resolve()), 'compress=gzip',
                        str(raw), str(compressed_path)], check=True)
        compressed = compressed_path.read_bytes()
    require(len(compressed) <= capacity and gzip.decompress(compressed) == rebuilt,
            'repacked ramdisk differs or exceeds original capacity')
    image = bytearray(source)
    image[offset:offset + capacity] = compressed + bytes(capacity - len(compressed))
    struct.pack_into('<I', image, 16, len(compressed))
    image[576:596] = image_id(image)
    require(image[:16] == source[:16] and image[20:576] == source[20:576] and
            image[596:offset] == source[596:offset] and
            image[offset + capacity:] == source[offset + capacity:], 'unrelated component changed')
    with output.open('xb') as stream:
        stream.write(image)
    print(hashlib.sha256(image).hexdigest())


if __name__ == '__main__':
    main()
