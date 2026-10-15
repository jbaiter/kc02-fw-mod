#!/usr/bin/env python3
"""Build RAM-only Luma applications for the three AX3292 boards. No flashing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BOARDS = ('original', 'x8d', 'z12')

def tool_prefix():
    if os.environ.get('OR1K_TOOL_PREFIX'):
        return os.environ['OR1K_TOOL_PREFIX']
    if shutil.which('or1k-elf-gcc'):
        return 'or1k-elf-'
    return str(Path.home() / '.cache/kc02-toolchain/prefix/bin/or1k-elf-')

def build(output, prefix):
    output.mkdir(parents=True, exist_ok=True)
    cc, objcopy = prefix+'gcc', prefix+'objcopy'
    flags = ['-mno-delay', '-mhard-div', '-msoft-float', '-mredzone=0',
             '-Os', '-g3', '-std=gnu99', '-ffreestanding', '-fno-builtin',
             '-fno-pic', '-fno-stack-protector', '-ffunction-sections',
             '-fdata-sections', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
             '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'include')]
    sources = [ROOT/'boards/start.S', ROOT/'boards/ax3292.c',
               *(ROOT/'src'/n for n in ('ui.c','paint.c','theme.c','input.c','storage.c','sd_hw.c','crc.c'))]
    manifests = {}
    for board_id, board in enumerate(BOARDS):
        folder = output/board
        folder.mkdir(exist_ok=True)
        elf, raw = folder/'luma.elf', folder/'luma.ram.bin'
        cmd = [cc, *flags, f'-DBOARD_ID={board_id}', '-nostdlib',
               '-Wl,--gc-sections', '-Wl,-e,_start', '-Wl,-Map,'+str(folder/'luma.map'),
               '-T', str(ROOT/'boards/ram.ld'), *map(str, sources), '-o', str(elf)]
        subprocess.run(cmd, check=True)
        header = elf.read_bytes()[:52]
        if header[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H',header,18)[0] != 92:
            raise ValueError('compiler must emit ELF32 little-endian OpenRISC')
        if struct.unpack_from('<I',header,36)[0] != 1 or struct.unpack_from('<I',header,24)[0] != 0x02000000:
            raise ValueError('wrong no-delay flags / entry')
        subprocess.run([objcopy,'-O','binary',str(elf),str(raw)], check=True)
        sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
        record = dict(board=board, entry=0x02000000, load=0x02000000,
                      size=raw.stat().st_size, sha256=sha(raw), elf_sha256=sha(elf),
                      kind='RAM DEVELOPMENT ONLY - NOT FLASHABLE', command=cmd,
                      sources={str(p.relative_to(ROOT)): sha(p) for p in sorted(ROOT.rglob('*'))
                               if p.suffix in ('.c','.h','.inc','.ld','.S') and 'build' not in p.parts})
        (folder/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
        manifests[board] = record
        print(f'{board}: {record["size"]:,} bytes -> {raw}')
    return manifests

BENCH_LENGTH = 0x140000   # 1.31 MiB: text+data+bss+stack for the original board

def bench_ld(base):
    """Linker script for the bounded hardware RAM run at an explicit base."""
    return f'''/* Generated: RAM-run bench image. Not a bootloader or flash/update image. */
OUTPUT_FORMAT("elf32-or1kle")
ENTRY(_start)
MEMORY {{ ram (rwx) : ORIGIN = {hex(base)}, LENGTH = {hex(BENCH_LENGTH)} }}
SECTIONS {{
  .text : {{ KEEP(*(.text.start)) KEEP(*(.text.bench)) *(.text*) }} > ram
  .rodata : {{ *(.rodata*) }} > ram
  .data : {{ KEEP(*(.data.bench)) *(.data*) }} > ram
  .bss (NOLOAD) : {{
    . = ALIGN(4); __bss_start = .; *(.bss*) *(COMMON)
    . = ALIGN(4); __bss_end = .;
  }} > ram
  __stack_top = ORIGIN(ram) + LENGTH(ram) - 16;
  ASSERT(__bss_end < __stack_top - 16384, "bench RAM budget exceeded")
  /DISCARD/ : {{ *(.comment) *(.eh_frame*) }}
}}
'''

def build_bench(output, prefix, base):
    """Build the bounded RAM-run image and record the symbols the loader needs."""
    folder = output/'original'
    folder.mkdir(parents=True, exist_ok=True)
    cc, objcopy, nm = prefix+'gcc', prefix+'objcopy', prefix+'nm'
    ld = folder/'bench.ld'
    ld.write_text(bench_ld(base))
    flags = ['-mno-delay', '-mhard-div', '-msoft-float', '-mredzone=0',
             '-Os', '-g3', '-std=gnu99', '-ffreestanding', '-fno-builtin',
             '-fno-pic', '-fno-stack-protector', '-ffunction-sections',
             '-fdata-sections', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
             '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'include'), '-DLUMA_BENCH_RUN']
    sources = [ROOT/'boards/start.S', ROOT/'boards/ax3292.c',
               *(ROOT/'src'/n for n in ('ui.c','paint.c','theme.c','input.c','storage.c','sd_hw.c','crc.c'))]
    elf, raw = folder/'luma.bench.elf', folder/'luma.bench.bin'
    cmd = [cc, *flags, '-DBOARD_ID=0', '-nostdlib', '-Wl,--gc-sections', '-Wl,-e,_start',
           '-Wl,-Map,'+str(folder/'luma.bench.map'), '-T', str(ld), *map(str, sources), '-o', str(elf)]
    subprocess.run(cmd, check=True)
    subprocess.run([objcopy, '-O', 'binary', str(elf), str(raw)], check=True)
    wanted = ('_start', 'luma_bench_once', '__bss_start', '__bss_end', '__stack_top',
              'luma_beacon', 'sensor_y', 'sensor_uv', 'lcd_y', 'lcd_uv')
    out = subprocess.run([nm, '-n', str(elf)], check=True, capture_output=True, text=True).stdout
    symbols = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in wanted:
            symbols[parts[2]] = int(parts[0], 16)
    missing = [w for w in wanted if w not in symbols]
    if missing:
        raise ValueError('bench image is missing symbols: ' + ', '.join(missing))
    if symbols['__bss_start'] < base or symbols['__bss_end'] > base + BENCH_LENGTH:
        raise ValueError('bench image escapes its declared RAM window')
    if symbols['_start'] != base:
        raise ValueError('_start is not at the image base; the takeover jump would be wrong')
    if not base <= symbols['luma_bench_once'] < symbols['__bss_start']:
        raise ValueError('bench entry is not inside the image text region')
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    record = dict(board='original', kind='RAM RUN ONLY - NOT FLASHABLE', base=base,
                  extent=BENCH_LENGTH, entry=symbols['luma_bench_once'],
                  start=symbols['_start'],
                  sensor_y=symbols['sensor_y'], sensor_uv=symbols['sensor_uv'],
                  beacon=symbols['luma_beacon'], bss_start=symbols['__bss_start'],
                  bss_end=symbols['__bss_end'], stack_top=symbols['__stack_top'],
                  size=raw.stat().st_size, sha256=sha(raw), elf_sha256=sha(elf), command=cmd)
    (folder/'bench.json').write_text(json.dumps(record, indent=2)+'\n')
    print(f'bench: {record["size"]:,} bytes @ {hex(base)}  entry={hex(record["entry"])} '
          f'bss={hex(record["bss_start"])}..{hex(record["bss_end"])}')
    return record

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool-prefix', default=tool_prefix())
    p.add_argument('--output', type=Path, default=ROOT/'build')
    p.add_argument('--bench-base', type=lambda v: int(v, 0), default=None,
                   help='also build the bounded RAM-run image at this address')
    a = p.parse_args()
    build(a.output.resolve(), a.tool_prefix)
    if a.bench_base is not None:
        if not 0x02104000 <= a.bench_base <= 0x02700000:
            raise SystemExit('--bench-base must be inside the firmware allocator pool '
                             '(0x02104000..0x02700000)')
        build_bench(a.output.resolve(), a.tool_prefix, a.bench_base)

if __name__ == '__main__':
    main()
