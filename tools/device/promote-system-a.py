#!/usr/bin/env python3
"""Plan by default; explicit system_a-only writes with an owner-local contract."""
import argparse
import hashlib
import json
import os
from pathlib import Path
from fastboot_transport import Fastboot
from sparse_image import BLOCK, encode_region

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda: f.read(1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()


def external(path):
    path = Path(path).resolve()
    if path == ROOT or ROOT in path.parents:
        raise ValueError('images, contracts and receipts belong outside Git')
    return path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--contract', required=True)
    p.add_argument('--receipt', required=True, help='new external directory')
    p.add_argument('--write-system-a', action='store_true')
    p.add_argument('--libusb')
    args = p.parse_args()
    contract = json.loads(external(args.contract).read_text())
    image = external(contract['image'])
    rollback = external(contract['rollback_image'])
    baseline = external(contract['baseline'])
    c = contract
    if c['schema'] != 1 or c['product'] != 'ATILIM_mPAD07':
        raise ValueError('this profile is Aura C only')
    if (sha(image) != c['image_sha256'] or image.stat().st_size != c['image_size'] or
            sha(rollback) != c['rollback_sha256'] or sha(baseline) != c['baseline_sha256']):
        raise ValueError('candidate, rollback or baseline changed')
    if c['dsu_qualified_sha256'] != c['image_sha256']:
        raise ValueError('DSU qualification must name this exact raw candidate')
    b = json.loads(baseline.read_text())
    if (b['system_a_sha256'] != c['rollback_sha256'] or
            b['system_a_size'] != c['system_a_size'] or
            b['boot_a_sha256'] != c['boot_a_sha256'] or
            b['boot_control_hex'] != c['boot_control_hex']):
        raise ValueError('permanent baseline does not bind rollback/recovery/boot control')
    if (type(c['image_size']) is not int or c['image_size'] <= 0 or
            c['image_size'] % BLOCK or rollback.stat().st_size != c['system_a_size']):
        raise ValueError('invalid image geometry')
    receipt = external(args.receipt)
    if receipt.exists():
        raise ValueError('receipt must be new; no automatic replay')
    print('Plan: resize and write only system_a; no reboot or other partition changes.')
    print(f"Candidate: {c['image_size']} bytes, SHA-256 {c['image_sha256']}")
    if not args.write_system_a:
        return
    receipt.mkdir(parents=True)
    (receipt / 'contract.json').write_text(json.dumps(c, indent=2) + '\n')
    fb = Fastboot(c['fastbootd_serial'], args.libusb)
    try:
        expected = {'product': c['product'], 'is-userspace': 'yes', 'unlocked': 'yes',
                    'current-slot': 'a', 'slot-successful:a': 'yes', 'slot-unbootable:a': 'no',
                    'snapshot-update-status': 'none', 'is-logical:system_a': 'yes'}
        for name, value in expected.items():
            if fb.getvar(name) != value:
                raise ValueError('fastboot preflight mismatch: ' + name)
        if (int(fb.getvar('partition-size:system_a'), 16) != c['system_a_size'] or
                int(fb.getvar('partition-size:super'), 16) != c['super_size']):
            raise ValueError('partition geometry changed')
        maximum = int(fb.getvar('max-download-size'), 16)
        region_size = 32 * 1024 * 1024
        if maximum < region_size + 1024 * 1024:
            raise ValueError('download capacity below reviewed region envelope')
        with (receipt / 'WRITE.started').open('x') as claim:
            claim.write(c['image_sha256'] + '\n'); claim.flush(); os.fsync(claim.fileno())
        fb.okay(f"resize-logical-partition:system_a:{c['image_size']}")
        if int(fb.getvar('partition-size:system_a'), 16) != c['image_size']:
            raise ValueError('resize readback mismatch')
        with image.open('rb') as source, (receipt / 'regions.jsonl').open('x') as log:
            offset = 0
            streamed_hash = hashlib.sha256()
            while offset < c['image_size']:
                data = source.read(min(region_size, c['image_size'] - offset))
                if not data or len(data) % BLOCK:
                    raise ValueError('candidate changed or truncated during write')
                sparse = encode_region(data, offset // BLOCK, c['image_size'] // BLOCK)
                if len(sparse) > maximum:
                    raise ValueError('sparse region exceeds download limit')
                fb.download(sparse)
                fb.okay('flash:system_a')
                streamed_hash.update(data)
                log.write(json.dumps({'offset': offset, 'bytes': len(data),
                                      'sha256': hashlib.sha256(data).hexdigest()}) + '\n')
                log.flush(); os.fsync(log.fileno()); offset += len(data)
            if source.read(1) or streamed_hash.hexdigest() != c['image_sha256']:
                raise ValueError('candidate changed during operation; full readback required')
        (receipt / 'ACCEPTED.txt').write_text('All writes accepted; independent readback and two boots still required.\n')
    finally:
        fb.close()


if __name__ == '__main__':
    main()
