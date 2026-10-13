#!/usr/bin/env python3
"""Localise and characterise code that is new in a newer KC02-class build.

Two signals, both from data already produced by the naming pipeline:

1. **Where** the new code is. The propagated names are anchor pairs
   (original address -> target address) for functions that are unchanged between products, which is
   effectively a relocation map. Where the gap between consecutive anchors is much larger in the
   target than in the original, the extra bytes are code with no counterpart in the original.

2. **What** it is. The inserted regions carry no inline strings (the strings live in the resource
   blob), so regions are characterised by which *named* functions they call. That is usually enough
   to tell UI code from sensor code from printer code without decompiling anything.

Usage:
    new_regions.py <functions.tsv> <names.tsv> <program> <image> [--min-bytes N] [--top N]
"""
import argparse
import struct
from collections import Counter
from pathlib import Path

MAP = 0x01FFDA00
JAL = 0x01


def load_rows(path, minimum=3):
    for line in Path(path).read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        parts = line.split('\t')
        if len(parts) >= minimum:
            yield parts


def monotonic(pairs):
    """Longest increasing run by (target, original), so the relocation map stays sane."""
    best = []
    for pair in sorted(pairs, key=lambda p: p[1]):
        if not best or pair[0] > best[-1][0]:
            best.append(pair)
    return best


def jal_target(image, offset):
    word = struct.unpack_from('<I', image, offset)[0]
    if (word >> 26) != JAL:
        return None
    imm = word & 0x03FFFFFF
    if imm >> 25:
        imm -= 1 << 26
    return (offset + MAP) + (imm << 2)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('functions', type=Path)
    ap.add_argument('names', type=Path)
    ap.add_argument('program')
    ap.add_argument('image', type=Path)
    ap.add_argument('--min-bytes', type=int, default=1024)
    ap.add_argument('--top', type=int, default=10)
    a = ap.parse_args()

    anchors = monotonic([(int(r[0], 16), int(r[1], 16), r[2]) for r in load_rows(a.names)])
    names = {}
    for r in load_rows(a.functions, 4):
        if r[0] == a.program:
            names[int(r[1].split(':')[-1], 16)] = r[2]
    named = sum(1 for n in names.values() if not n.startswith(('FUN_', 'thunk_')))
    unnamed = sorted(addr for addr, n in names.items() if n.startswith(('FUN_', 'thunk_')))
    image = a.image.read_bytes()

    regions = []
    for (o1, t1, n1), (o2, t2, n2) in zip(anchors, anchors[1:]):
        inserted = (t2 - t1) - (o2 - o1)
        if inserted >= a.min_bytes:
            regions.append((inserted, t1, t2, n1, n2))
    regions.sort(reverse=True)

    print(f'{a.program}')
    print(f'  {len(anchors)} monotonic anchors, {named} named / {len(unnamed)} unnamed functions')
    total = sum(r[0] for r in regions)
    print(f'  {len(regions)} inserted regions >= 0x{a.min_bytes:x};'
          f' total inserted 0x{total:x} bytes\n')
    print(f'  {"region":>21}  {"inserted":>9}  {"unn":>4}  callees')
    for inserted, start, end, before, after in regions[:a.top]:
        counts, unnamed_calls = Counter(), 0
        for i in range(start - MAP, min(end - MAP, len(image) - 4), 4):
            target = jal_target(image, i)
            if target is None or target not in names:
                continue
            name = names[target]
            if name.startswith(('FUN_', 'thunk_')):
                unnamed_calls += 1
            else:
                counts[name] += 1
        inside = sum(1 for u in unnamed if start <= u < end)
        callees = ', '.join(f'{n} x{c}' for n, c in counts.most_common(4)) or '(no named callees)'
        print(f'  0x{start:08X}..0x{end:08X}  0x{inserted:07X}  {inside:4d}  {callees}')
        print(f'      sits between {before} and {after}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
