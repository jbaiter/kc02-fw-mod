#!/usr/bin/env python3
"""Build the original KC02 recovery/debug first-stage RAM candidate. Never flash.

No USB identity is assigned by this project. Defaults leave USB detached. Supply
an explicitly selected experimental VID/PID pair to compile the polled backend.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from build import ROOT, tool_prefix


def build(output, prefix, vid=0, pid=0, selftest=False):
    if not 0 <= vid <= 0xffff or not 0 <= pid <= 0xffff or bool(vid) != bool(pid):
        raise ValueError('provide both nonzero 16-bit USB identifiers, or neither')
    if selftest and (vid or pid):
        raise ValueError('self-test cannot attach USB')
    # Never masquerade as either stock KC02 identity.
    if (vid, pid) in {(0x0219, 0x3280), (0x1908, 0x3282), (0x1908, 0x3283)}:
        raise ValueError('choose a distinct development identity, not a stock-camera VID/PID')
    output.mkdir(parents=True, exist_ok=True)
    elf, raw = output/'bringup.elf', output/'bringup.ram.bin'
    sources = [ROOT/'bringup'/n for n in ('start.S', 'vectors.S', 'checkpoint.S', 'main.c', 'runtime.c')]
    if selftest:
        sources += [ROOT/'bringup'/n for n in ('selftest.S', 'selftest.c')]
    sources += [ROOT/'src'/n for n in ('debug.c', 'monitor.c', 'usb_device.c', 'ax_usb.c',
                                      'sd_hw.c', 'storage.c', 'destbin.c', 'recover.c', 'or1k_decode.c',
                                      'msc.c', 'msc_medium.c', 'update_sd.c', 'update.c', 'crc.c')]
    flags = ['-mno-delay', '-mhard-div', '-msoft-float', '-mredzone=0', '-Os', '-g3',
             '-std=gnu99', '-ffreestanding', '-fno-builtin', '-fno-pic', '-fno-stack-protector',
             '-ffunction-sections', '-fdata-sections', '-fno-unwind-tables',
             '-fno-asynchronous-unwind-tables', '-Wall', '-Wextra', '-Werror',
             '-I'+str(ROOT/'include'), f'-DUSB_VID={vid}', f'-DUSB_PID={pid}',
             f'-DBRINGUP_SELFTEST={int(selftest)}', '-nostdlib', '-Wl,--gc-sections',
             '-Wl,-e,_start', '-Wl,-Map,'+str(output/'bringup.map'),
             '-T', str(ROOT/'bringup/original.ld')]
    command = [prefix+'gcc', *flags, *map(str, sources), '-o', str(elf)]
    subprocess.run(command, check=True)
    header = elf.read_bytes()[:52]
    if (header[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H', header, 18)[0] != 92 or
            struct.unpack_from('<I', header, 24)[0] != 0x0200046c or
            struct.unpack_from('<I', header, 36)[0] != 1):
        raise ValueError('incorrect ELF machine/endian/entry/no-delay ABI')
    subprocess.run([prefix+'objcopy', '-O', 'binary', str(elf), str(raw)], check=True)
    relevant = sources + list((ROOT/'include').glob('*.h')) + [ROOT/'bringup/original.ld', Path(__file__)]
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    record = dict(board='original', kind='UNQUALIFIED BRINGUP RAM', load=0x02000000,
                  entry=0x0200046c, resident=0x027c0000, app_stack=0x027b0000,
                  monitor_stack=0x027ffff0, observations=0x02002800,
                  size=raw.stat().st_size, sha256=sha(raw), elf_sha256=sha(elf),
                  usb_vid=vid, usb_pid=pid, selftest=selftest,
                  usb_hardware_verified=False, recovery_writer=False, flash_qualified=False,
                  sources={str(p.relative_to(ROOT)): sha(p) for p in relevant}, command=command)
    (output/'manifest.json').write_text(json.dumps(record, indent=2)+'\n')
    print(f'original bring-up: {record["size"]:,} bytes -> {raw} (NOT FLASH-QUALIFIED)')
    return record


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool-prefix', default=tool_prefix())
    p.add_argument('--output', type=Path, default=ROOT/'build/bringup-original')
    p.add_argument('--usb-vid', type=lambda s: int(s, 0), default=0)
    p.add_argument('--usb-pid', type=lambda s: int(s, 0), default=0)
    p.add_argument('--selftest', action='store_true', help='QEMU checkpoint loop only, never factory-packaged')
    a = p.parse_args()
    try:
        build(a.output.resolve(), a.tool_prefix, a.usb_vid, a.usb_pid, a.selftest)
    except ValueError as exc:
        p.error(str(exc))


if __name__ == '__main__':
    main()
