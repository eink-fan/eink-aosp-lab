#!/usr/bin/env python3
"""Read-only root-ADB baseline/readback capture. Never enables DSU or reboots."""
import argparse
import json
import re
import subprocess
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    p.add_argument('--output', required=True)
    p.add_argument('--verify-baseline')
    p.add_argument('--candidate-sha256')
    p.add_argument('--candidate-size', type=int)
    p.add_argument('--physical-boot', action='store_true')
    args = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = Path(args.output).resolve()
    if output == root or root in output.parents or output.exists():
        raise ValueError('output must be new and outside the repository')
    def shell(command):
        return subprocess.check_output(['adb', '-s', args.serial, 'shell', command],
                                       text=True, timeout=600).strip()
    def digest(node):
        result = shell('sha256sum ' + node).split()[0]
        if not re.fullmatch('[0-9a-f]{64}', result):
            raise ValueError('invalid device hash')
        return result
    if shell('id -u') != '0' or shell('getprop ro.product.device') != 'ATILIM_mPAD07':
        # The generic system can report its own product identity; bind vendor too.
        if shell('id -u') != '0' or shell('getprop ro.product.vendor.device') != 'ATILIM_mPAD07':
            raise ValueError('require authorized root ADB and matched Aura product')
    record = {
        'system_a_sha256': digest('/dev/block/mapper/system_a'),
        'system_a_size': int(shell('blockdev --getsize64 /dev/block/mapper/system_a')),
        'boot_a_sha256': digest('/dev/block/by-name/boot_a'),
        'boot_control_hex': ''.join(shell('dd if=/dev/block/by-name/para bs=1 skip=2048 count=32 '
                                         '2>/dev/null | od -An -v -tx1').split()),
        'boot_id': shell('cat /proc/sys/kernel/random/boot_id'),
        'boot_complete': shell('getprop sys.boot_completed'),
        'slot': shell('getprop ro.boot.slot_suffix'),
        'selinux': shell('getenforce'),
        'dsu_running': shell('getprop ro.gsid.image_running'),
        'sdk': shell('getprop ro.build.version.sdk'),
        'device_provisioned': shell('settings get global device_provisioned'),
        'user_setup_complete': shell('settings get secure user_setup_complete'),
        'services': shell('service list'),
    }
    if len(record['boot_control_hex']) != 64:
        raise ValueError('invalid boot-control readback')
    if args.verify_baseline:
        baseline = json.loads(Path(args.verify_baseline).read_text())
        if not args.candidate_sha256 or not args.candidate_size:
            raise ValueError('verification requires candidate identity')
        for key in ['boot_a_sha256', 'boot_control_hex', 'device_provisioned', 'user_setup_complete']:
            if record[key] != baseline[key]:
                raise ValueError('baseline mismatch: ' + key)
        if (record['system_a_sha256'] != args.candidate_sha256 or
                record['system_a_size'] != args.candidate_size):
            raise ValueError('physical partition differs from candidate')
    if args.physical_boot:
        if (record['boot_complete'] != '1' or record['slot'] != '_a' or
                record['selinux'] != 'Enforcing' or record['dsu_running'] in ('1', 'true')):
            raise ValueError('not a complete enforcing physical-A boot')
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x') as f:
        json.dump(record, f, indent=2); f.write('\n')


if __name__ == '__main__':
    main()
