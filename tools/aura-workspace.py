#!/usr/bin/env python3
"""Owner-local Aura payload and fresh Android-14 workspace preparation."""
import argparse
import hashlib
import json
import platform
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / 'profiles/aura-c'


def run(*args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()


def outside(value):
    path = Path(value).expanduser().resolve()
    if path == ROOT or ROOT in path.parents:
        raise ValueError('payloads/workspaces must be outside this repository')
    return path


def lock():
    return {line.split()[1]: line.split()[0] for line in
            (PROFILE / 'vendor-files.sha256').read_text().splitlines() if line.strip()}


def calibration(levels):
    if (not isinstance(levels, list) or len(levels) != 31 or
            any(type(v) is not int or not 0 <= v <= 255 for v in levels) or
            levels[0] != 0 or any(a >= b for a, b in zip(levels, levels[1:]))):
        raise ValueError('expected 31 increasing owner-extracted integer codes starting at zero')
    base = '/sys/devices/platform/11009000.i2c/i2c-2/'
    return ('schema=neo2-frontlight-direct-v1\ncalibration_id=owner-aura-c\n' +
            'brightness_codes=' + ','.join(map(str, levels)) + '\n' +
            ''.join(f'{color}_{which}_path={base}2-{chip}/{node}_brightness\n'
                    for color, node in [('cold', 'b'), ('warm', 'a')]
                    for which, chip in [('primary', '0036'), ('secondary', '0038')]))


def verify_payload(payload):
    if payload.is_symlink() or any(p.is_symlink() for p in payload.rglob('*')):
        raise ValueError('symlinks are not payload files')
    expected = lock()
    for relative, digest in expected.items():
        path = payload / relative
        if not path.is_file() or sha(path) != digest:
            raise ValueError(f'payload mismatch: {relative}')
    levels = json.loads((payload / 'frontlight/levels.json').read_text())
    if (payload / 'frontlight/aura_c_frontlight_calibration.conf').read_text() != calibration(levels):
        raise ValueError('calibration differs from owner-provided levels')
    source = json.loads((payload / 'source.json').read_text())
    if source != {'device': 'ATILIM_mPAD07', 'sdk': '30'}:
        raise ValueError('payload source must be the matched stock Aura API-30 runtime')
    allowed = set(expected) | {'frontlight/levels.json',
            'frontlight/aura_c_frontlight_calibration.conf', 'source.json'}
    if {str(p.relative_to(payload)) for p in payload.rglob('*') if p.is_file()} != allowed:
        raise ValueError('unexpected payload files')


def extract(args):
    output = outside(args.output)
    if output.exists():
        raise ValueError('output must be new')
    levels = json.loads(Path(args.levels).read_text())
    config = calibration(levels)
    adb = ['adb', '-s', args.serial]
    source = {}
    for key, prop in [('device', 'ro.product.device'), ('sdk', 'ro.build.version.sdk')]:
        source[key] = subprocess.check_output(adb + ['shell', 'getprop', prop], text=True).strip()
    if source != {'device': 'ATILIM_mPAD07', 'sdk': '30'}:
        raise ValueError('connected device is not the matched stock Aura baseline')
    output.mkdir(parents=True)
    for relative in lock():
        dest = output / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        name = dest.name
        remote = ('/vendor/etc/' + name if relative.startswith('vendor-prerequisites/')
                  else '/system/etc/' + name if relative.startswith('waveforms/')
                  else '/system/lib64/' + name)
        run(*adb, 'pull', remote, dest)
    (output / 'frontlight').mkdir()
    (output / 'frontlight/levels.json').write_text(json.dumps(levels) + '\n')
    (output / 'frontlight/aura_c_frontlight_calibration.conf').write_text(config)
    (output / 'source.json').write_text(json.dumps(source) + '\n')
    verify_payload(output)


def tree_lock(directory):
    return {str(p.relative_to(directory)): sha(p) for p in sorted(directory.rglob('*'))
            if p.is_file() and '.git' not in p.parts}


def project_state(aosp, relative):
    path = aosp / relative
    def git(*args):
        return subprocess.check_output(['git', '-C', str(path), *args])
    untracked = git('ls-files', '--others', '--exclude-standard', '-z').decode().split('\0')
    return {'head': git('rev-parse', 'HEAD').decode().strip(),
            'diff': hashlib.sha256(git('diff', 'HEAD', '--binary')).hexdigest(),
            'untracked': {p: sha(path / p) for p in untracked if p and
                          not p.startswith('services/surfaceflinger/Neo2Eink/')}}


def verify_workspace(workspace):
    receipt = json.loads((workspace / 'PREPARED.json').read_text())
    if receipt['schema'] != 1 or receipt['profile'] != 'aura-c-android14':
        raise ValueError('wrong workspace profile')
    if sha(workspace / 'resolved-manifest.xml') != receipt['manifest']:
        raise ValueError('manifest drift')
    aosp = workspace / 'aosp'
    graft = aosp / 'frameworks/native/services/surfaceflinger/Neo2Eink'
    if tree_lock(graft) != receipt['graft']:
        raise ValueError('graft/payload drift')
    for relative, expected in receipt['projects'].items():
        if project_state(aosp, relative) != expected:
            raise ValueError(f'framework source drift: {relative}')


def prepare(args):
    payload, workspace = outside(args.payload), outside(args.workspace)
    verify_payload(payload)
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        raise ValueError('AOSP preparation requires Linux x86_64')
    if workspace.exists():
        raise ValueError('workspace must be new; partial preparations are never silently reused')
    parent = workspace.parent
    while not parent.exists():
        parent = parent.parent
    if shutil.disk_usage(parent).free < 300 * 1024**3:
        raise ValueError('at least 300 GiB free required for fresh checkout and output')
    for tool in ['repo', 'git', 'python3', 'make', 'javac']:
        if not shutil.which(tool):
            raise ValueError(f'missing host tool: {tool}')
    workspace.mkdir(parents=True)
    aosp = workspace / 'aosp'
    aosp.mkdir()
    run('repo', 'init', '-u', 'https://android.googlesource.com/platform/manifest',
        '-b', 'e3f9241982f55ef4cec97df0c55e9b87d37a5656', '-m', 'default.xml', cwd=aosp)
    shutil.copyfile(PROFILE / 'aosp-resolved-manifest.xml', aosp / '.repo/manifests/default.xml')
    run('repo', 'sync', '-c', '--no-tags', '--no-clone-bundle', f'-j{args.jobs}', cwd=aosp)
    # Pin verification is independent of the mutable upstream tag names.
    import xml.etree.ElementTree as ET
    for project in ET.parse(PROFILE / 'aosp-resolved-manifest.xml').getroot().findall('project'):
        path = aosp / project.get('path', project.attrib['name'])
        actual = subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != project.attrib['revision']:
            raise ValueError(f'resolved project mismatch: {path.relative_to(aosp)}')
    run('repo', 'manifest', '-r', '-o', workspace / 'resolved-manifest.xml', cwd=aosp)
    for stage in ['base', 'aura', 'controls']:
        for patch in sorted((PROFILE / 'patches' / stage).rglob('*.patch')):
            project = aosp / str(patch.relative_to(PROFILE / 'patches' / stage))[:-6]
            run('git', 'apply', '--check', patch, cwd=project)
            run('git', 'apply', patch, cwd=project)
    graft = aosp / 'frameworks/native/services/surfaceflinger/Neo2Eink'
    shutil.copytree(ROOT / 'android/device-eink', graft)
    shutil.copytree(ROOT / 'android/ink-controls', graft / 'android/frontlight-app')
    runtime = graft / 'android/vendor-runtime/aura_c'
    runtime.mkdir(parents=True)
    for relative in lock():
        target = runtime / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(payload / relative, target)
    (runtime / 'frontlight').mkdir()
    shutil.copyfile(payload / 'frontlight/aura_c_frontlight_calibration.conf',
                    runtime / 'frontlight/aura_c_frontlight_calibration.conf')
    for path in (PROFILE / 'runtime').iterdir():
        shutil.copyfile(path, runtime / path.name)
    # Include the complete source and payload staging in the preparation receipt.
    receipt = {'schema': 1, 'profile': 'aura-c-android14',
               'manifest': sha(workspace / 'resolved-manifest.xml'), 'graft': tree_lock(graft),
               'projects': {p: project_state(aosp, p) for p in
                   ['build/make', 'build/soong', 'frameworks/base', 'frameworks/native',
                    'system/sepolicy', 'packages/modules/Bluetooth']}}
    (workspace / 'PREPARED.json').write_text(json.dumps(receipt, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('extract')
    p.add_argument('--serial', required=True)
    p.add_argument('--levels', required=True, help='owner-extracted stock 31-code JSON array')
    p.add_argument('--output', required=True)
    p = sub.add_parser('verify-payload')
    p.add_argument('--payload', required=True)
    p = sub.add_parser('prepare')
    p.add_argument('--payload', required=True)
    p.add_argument('--workspace', required=True)
    p.add_argument('--jobs', type=int, default=4)
    p = sub.add_parser('verify-workspace')
    p.add_argument('--workspace', required=True)
    args = parser.parse_args()
    if getattr(args, 'jobs', 1) < 1:
        parser.error('jobs must be positive')
    if args.command == 'extract':
        extract(args)
    elif args.command == 'prepare':
        prepare(args)
    elif args.command == 'verify-workspace':
        verify_workspace(outside(args.workspace))
    else:
        verify_payload(outside(args.payload))


if __name__ == '__main__':
    main()
