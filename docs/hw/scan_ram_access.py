#!/usr/bin/env python3
"""Derive RAM global addresses, widths and access counts from the firmware listing.

Region tables used for hardware observation should be evidence-based: this scans for the
standard constant-materialisation pair (`l.movhi rN,0x020d` .. `l.ori rN,rN,<offset>`) and then
records which offsets of that register the following instructions touch, plus the access width
from the opcode (`l.lbz`/`l.sb` = 1 byte, `l.lhz`/`l.sh` = 2, `l.lwz`/`l.sw` = 4).

Usage:
    scan_ram_access.py <disassembly> <first> <last> [--window N]

Addresses are CPU VMAs, e.g. 0x020D2E80 0x020D2F20. A global reported as one byte wide with one
site is still a real global, but only that extent has evidence; do not assume neighbours belong to
the same structure.
"""
import argparse
import re
from collections import defaultdict
from pathlib import Path

WIDTH = {'lbz': 1, 'lbs': 1, 'sbz': 1, 'sb': 1,
         'lhz': 2, 'lhs': 2, 'shz': 2, 'sh': 2,
         'lwz': 4, 'lws': 4, 'swz': 4, 'sw': 4}
OP = re.compile(r'\s*([0-9a-f]+):.*\bl\.(movhi|ori)\s+(r\d+),(?:r\d+,)?(0x[0-9a-f]+|\d+)\s*$')
# loads are `l.lwz rD,off(rB)`; stores are `l.sw off(rB),rS`.
MEM = re.compile(r'\bl\.(%s)\s+(?:r\d+,\s*)?(-?0x[0-9a-f]+|\d+)?\((%s)\)'
                 % ('|'.join(WIDTH), r'r\d+'))


def scan(text, first, last, window):
    lines = text.splitlines()
    globals_ = defaultdict(lambda: defaultdict(int))   # addr -> opcode -> count
    for index, line in enumerate(lines):
        match = OP.match(line)
        if not match or match.group(2) != 'ori':
            continue
        value = int(match.group(4), 0)
        if value < 0x2000:
            continue
        register = match.group(3)
        base = 0x020D0000 + value
        if not first <= base <= last:
            continue
        high = re.search(r'\bl\.movhi\s+%s,0x0*20d\s*$' % register, lines[index - 1]) or (
            index >= 2 and re.search(r'\bl\.movhi\s+%s,0x0*20d\s*$' % register, lines[index - 2]))
        if not high:
            continue
        for follow in lines[index:index + window]:
            access = MEM.search(follow)
            if access and access.group(3) == register:
                offset = access.group(2) or '0'
                offset = int(offset, 16) if offset.startswith('-0x') else int(offset, 16)
                globals_[base + offset][access.group(1)] += 1
    return globals_


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('disassembly', type=Path)
    ap.add_argument('first', type=lambda v: int(v, 0))
    ap.add_argument('last', type=lambda v: int(v, 0))
    ap.add_argument('--window', type=int, default=50,
                    help='instructions after the materialisation to attribute accesses over')
    args = ap.parse_args()
    found = scan(args.disassembly.read_text(errors='replace'), args.first, args.last, args.window)
    print(f'{len(found)} distinct addresses in 0x{args.first:08x}..0x{args.last:08x}')
    for address in sorted(found):
        ops = found[address]
        total = sum(ops.values())
        width = max(WIDTH[op] for op in ops)
        reads = sum(count for op, count in ops.items() if not op.startswith('s'))
        writes = total - reads
        detail = ', '.join(f'{op}x{count}' for op, count in sorted(ops.items()))
        print(f'  0x{address:08x}  width<={width}  {total:3d} site(s) ({reads} read, {writes} write)  {detail}')


if __name__ == '__main__':
    main()
