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
               *(ROOT/'src'/n for n in ('ui.c','paint.c','theme.c','input.c','storage.c'))]
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

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool-prefix',default=tool_prefix())
    p.add_argument('--output',type=Path,default=ROOT/'build')
    a = p.parse_args()
    build(a.output.resolve(), a.tool_prefix)

if __name__ == '__main__':
    main()
