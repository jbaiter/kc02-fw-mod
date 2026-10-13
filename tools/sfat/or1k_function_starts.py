#!/usr/bin/env python3
"""Extract function-start candidates from a KC02-class image by decoding OR1K jumps.

The raw-binary import gives Ghidra no entry points, so its analyzers only find whatever the
constant/data references happen to reach (426 of the X8D's ~2169 real functions). This finds the
starts directly from the instruction encoding instead:

  * every `l.jal` (opcode 0x01) target is a called function;
  * the instruction after `l.jr r9` (opcode 0x11) resembles the next function's prologue.

Both are candidates, not proofs: data that decodes as a jump contributes noise. The point is to
give Ghidra a superset of entry points so its own disassembly and analysis can take over.

Usage:
    or1k_function_starts.py <image> --first 0x02000000 --last 0x020D0C00 --out starts.txt
"""
import argparse
import struct
from pathlib import Path

JAL = 0x01
JR = 0x11
MAP = 0x01FFDA00          # CPU = file + 0x01FFDA00


def decode(image, first, last):
    calls, after_return, returns = set(), set(), 0
    for cpu in range(first, last - 4, 4):
        off = cpu - MAP
        if off < 0 or off + 4 > len(image):
            continue
        word = struct.unpack_from('<I', image, off)[0]
        op = word >> 26
        if op == JAL:
            imm = word & 0x03FFFFFF
            if imm >> 25:                      # sign-extend the 26-bit word offset
                imm -= 1 << 26
            target = cpu + (imm << 2)
            if first <= target < last:
                calls.add(target)
        elif op == JR and ((word >> 11) & 0x1F) == 9:   # l.jr r9 (rB at bits 15..11)
            returns += 1
            after_return.add(cpu + 4)
    return calls, after_return, returns


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('image', type=Path)
    ap.add_argument('--first', type=lambda v: int(v, 0), default=0x02000000)
    ap.add_argument('--last', type=lambda v: int(v, 0), default=0x020D0C00)
    ap.add_argument('--out', type=Path)
    a = ap.parse_args()
    image = a.image.read_bytes()
    calls, after_return, n_ret = decode(image, a.first, a.last)
    starts = sorted(calls | after_return)
    print(f'{a.image.name}')
    print(f'  range        0x{a.first:08X}..0x{a.last:08X}')
    print(f'  l.jal targets        {len(calls)}')
    print(f'  l.jr r9 returns      {n_ret}')
    print(f'  starts after return  {len(after_return)}')
    print(f'  union of candidates  {len(starts)}')
    if a.out:
        a.out.write_text('\n'.join(f'0x{s:08X}' for s in starts) + '\n')
        print(f'  written to {a.out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
