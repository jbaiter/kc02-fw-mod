#!/usr/bin/env python3
"""Build the custom UI routing patch into its three in-place image slots.

Also emits the slot blobs as raw .bin files next to the ELF, which is what patch_custom_ui.py
injects. Asserts each slot against the 100-byte budget: the pieces replace stock handlers one for
one and must not run into the next function.
"""
import argparse, hashlib, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SLOTS = {'slot_power': (0x02009F08, 100), 'slot_settings': (0x02016C14, 100), 'slot_playback': (0x0200BD4C, 100)}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tool-prefix', default='/tmp/ax3292-toolchain/prefix/bin/or1k-elf-')
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    tool = lambda n: a.tool_prefix + n
    obj, elf = a.out / 'ui_router.o', a.out / 'ui_router.elf'
    subprocess.run([tool('gcc'), '-mno-delay', '-mhard-div', '-Os', '-std=gnu11', '-ffreestanding',
                    '-fno-builtin', '-fno-common', '-fno-pic', '-fno-stack-protector',
                    '-Wall', '-Wextra', '-Werror', '-c', str(HERE / 'ui_router.c'), '-o', str(obj)],
                   check=True)
    subprocess.run([tool('ld'), '-T', str(HERE / 'ui_router.ld'), str(obj), '-o', str(elf),
                    '-Map=' + str(a.out / 'ui_router.map')], check=True)
    blobs = {}
    for name, (addr, budget) in SLOTS.items():
        raw = a.out / f'{name}.bin'
        subprocess.run([tool('objcopy'), '--dump-section', f'.{name}={raw}', str(elf)], check=True)
        data = raw.read_bytes()
        if len(data) > budget:
            raise SystemExit(f'{name} is {len(data)} B, over its {budget} B slot')
        blobs[name] = (addr, data)
        print(f'  {name:14s} {len(data):4d} B  (budget {budget})  -> 0x{addr:08X}')
    # the arming block's two entry points, recovered from the symbol table
    syms = subprocess.check_output([tool('nm'), str(elf)], text=True)
    entries = {}
    for line in syms.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in ('camera_arm_up', 'camera_arm_down', 'power_to_camera'):
            entries[parts[2]] = int(parts[0], 16)
    if set(entries) != {'camera_arm_up', 'camera_arm_down', 'power_to_camera'}:
        raise SystemExit('missing symbols: ' + repr(sorted(entries)))
    for name, addr in sorted(entries.items()):
        print(f'  {name:14s} at 0x{addr:08X}')
    (a.out / 'entries.json').write_text(
        '\n'.join(f'0x{k:08X}' for k in (entries['camera_arm_up'], entries['camera_arm_down'],
                                         entries['power_to_camera'])) + '\n')
    print(f'  elf sha256 {hashlib.sha256(elf.read_bytes()).hexdigest()[:16]}')


if __name__ == '__main__':
    sys.exit(main())
