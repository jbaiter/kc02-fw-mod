#!/usr/bin/env python3
"""Run the new RAM application in QEMU; never writes physical firmware."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent
sys.path.insert(0, str(REPO/'tools/emu'))
from profiles import PROFILES, machine_args


def command(board, build, qemu, *, sd=None, headless=False, qmp=None, gdb=None):
    folder = build/board
    raw = folder/'luma.ram.bin'
    manifest = json.loads((folder/'manifest.json').read_text())
    if manifest['board'] != board or hashlib.sha256(raw.read_bytes()).hexdigest() != manifest['sha256']:
        raise ValueError('build mismatch; rebuild the RAM application')
    # Board configuration only; deliberately no stock flash image or vectors.
    properties = PROFILES[board].options()
    properties.update(entry='0x02000000', **{'stack-top':'0x021ffff0', 'sensor':'on'})
    if sd:
        properties['sd-image'] = str(sd.resolve())
    cmd = [str(qemu), *machine_args(properties), '-m','64M', '-nodefaults',
           '-accel','tcg,thread=multi', '-kernel',str(raw),
           '-name',f'Luma / {board} / RAM development',
           '-display','none' if headless else 'sdl', '-monitor','none', '-serial','none']
    if qmp:
        cmd += ['-qmp',qmp]
    if gdb:
        if not 1 <= gdb <= 65535:
            raise ValueError('invalid GDB port')
        cmd += ['-gdb',f'tcp:127.0.0.1:{gdb}']
    return cmd


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--board',choices=PROFILES,default='original')
    p.add_argument('--build',type=Path,default=ROOT/'build')
    p.add_argument('--qemu',type=Path,default=Path.home()/'.src/qemu-11.1.2/build/qemu-system-or1k')
    p.add_argument('--sd-card',type=Path)
    p.add_argument('--headless',action='store_true')
    p.add_argument('--qmp')
    p.add_argument('--gdb',type=int,help='QEMU localhost GDB, NOT USB hardware debugging')
    a = p.parse_args()
    cmd = command(a.board,a.build.resolve(),a.qemu,sd=a.sd_card,headless=a.headless,qmp=a.qmp,gdb=a.gdb)
    print('RAM development image; no physical USB, printer, or updater yet.',flush=True)
    print('BACK: next screen; arrows: ink / photos / list; OK: capture / print / select.',flush=True)
    print('Hold OK: cycle ink (no print / dither / grayscale).',flush=True)
    try:
        return subprocess.run(cmd,env=dict(os.environ)).returncode
    except KeyboardInterrupt:
        return 130

if __name__ == '__main__':
    raise SystemExit(main())
