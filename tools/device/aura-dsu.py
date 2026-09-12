#!/usr/bin/env python3
"""Separate Aura DSU staging, disabled installation and one-boot enablement."""
import argparse
import hashlib
import re
import subprocess
from pathlib import Path

REMOTE = '/data/local/tmp/einklab-aura/system.img'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['stage', 'install', 'enable'])
    p.add_argument('--serial', required=True)
    p.add_argument('--image', help='local AVB/FEC package for staging')
    p.add_argument('--sha256', required=True)
    p.add_argument('--size', type=int, required=True)
    p.add_argument('--execute', action='store_true')
    args = p.parse_args()
    if not re.fullmatch('[0-9a-f]{64}', args.sha256) or args.size <= 0:
        raise ValueError('require exact package size and hash')
    if args.action == 'stage':
        if not args.image:
            raise ValueError('stage requires --image')
        image = Path(args.image).resolve()
        h = hashlib.sha256()
        with image.open('rb') as source:
            for data in iter(lambda: source.read(1024 * 1024), b''):
                h.update(data)
        if image.stat().st_size != args.size or h.hexdigest() != args.sha256:
            raise ValueError('local package mismatch')
    print(f'Plan: {args.action} only; no wipe, reboot, or physical partition write.')
    if not args.execute:
        return
    adb = ['adb', '-s', args.serial]
    def shell(command):
        return subprocess.check_output(adb + ['shell', command], text=True, timeout=1800).strip()
    if shell('id -u') != '0':
        raise ValueError('require authorized root ADB')
    if 'ATILIM_mPAD07' not in [shell('getprop ro.product.device'),
                              shell('getprop ro.product.vendor.device')]:
        raise ValueError('wrong model')
    sdk = shell('getprop ro.build.version.sdk')
    if sdk not in ('30', '34'):
        raise ValueError('unsupported running host')
    if shell('getprop ro.gsid.image_running') in ('1', 'true'):
        raise ValueError('return to physical host before this operation')
    if args.action == 'stage':
        shell('test ! -e /data/local/tmp/einklab-aura && mkdir -m 700 /data/local/tmp/einklab-aura')
        subprocess.run(adb + ['push', str(image), REMOTE], check=True)
        shell(f'chown 0:0 {REMOTE} && chmod 600 {REMOTE} && chcon u:object_r:gsi_data_file:s0 {REMOTE}')
    if (shell(f'stat -c %s {REMOTE}') != str(args.size) or
            shell(f'sha256sum {REMOTE}').split()[0] != args.sha256):
        raise ValueError('staged package mismatch')
    if args.action == 'stage':
        return
    status = shell('gsi_tool status')
    tokens = status.lower().split()
    if args.action == 'install':
        if 'normal' not in tokens or 'installed' in tokens or 'running' in tokens:
            raise ValueError('require clean normal DSU state; cleanup is a separate operation')
        descriptor = 1 if sdk == '30' else 0
        userdata = '' if sdk == '30' else ' --userdata-size 8589934592'
        shell(f'gsi_tool install --gsi-size {args.size}{userdata} --no-reboot {descriptor}< {REMOTE}')
        shell('gsi_tool disable')
        status = shell('gsi_tool status')
        if 'installed' not in status.lower().split() or 'disabled' not in status.lower().split():
            raise ValueError('disabled installation not confirmed; inspect without replaying')
        # The local receipt binds this package to the successful disabled install.
        shell(f'printf %s {args.sha256} > /data/local/tmp/einklab-aura/installed.sha256')
    else:
        if 'installed' not in tokens or 'disabled' not in tokens:
            raise ValueError('require installed-disabled state')
        if shell('cat /data/local/tmp/einklab-aura/installed.sha256') != args.sha256:
            raise ValueError('no matching disabled-install receipt')
        shell('gsi_tool enable -s')
    print(shell('gsi_tool status'))


if __name__ == '__main__':
    main()
