#!/usr/bin/env python3
"""Locate the Buildwin asset table (SFAT) in a KC02-class image, whatever its base.

The table format is:
    +0x00  u32 table_size
    +0x08  (u32 rel_offset, u32 size) x N, rel_offsets strictly increasing
    asset bytes at table_base + rel_offset

The table base moves between builds, so rather than hardcode it this searches for the
structural signature and validates it against the asset magics (JPEG/BMP/RIFF), which is
what makes a candidate believable.

Usage:
    find_sfat.py <image> [--extract DIR]
"""
import argparse
import struct
import sys
from pathlib import Path

MAGICS = {b'\xff\xd8': 'jpg', b'BM': 'bmp', b'RIFF': 'wav'}


def candidates(d):
    hits = []
    for base in range(0x2000, len(d) - 0x1000, 4):
        size = struct.unpack_from('<I', d, base)[0]
        if not 0x28 <= size <= 0x8000 or (size - 8) % 8:
            continue
        count = (size - 8) // 8
        if count < 4:
            continue
        rel = []
        ok = True
        for i in range(count):
            r, s = struct.unpack_from('<II', d, base + 8 + i * 8)
            if r == 0 or base + r + s > len(d) or s == 0 or s > 0x400000:
                ok = False
                break
            rel.append(r)
        if not ok or any(rel[i] >= rel[i + 1] for i in range(len(rel) - 1)):
            continue
        hits.append((base, size, count, rel))
    return hits


def score(d, base, rel):
    """How many of the entries actually start with a known container magic?"""
    good = 0
    kinds = {}
    for r in rel:
        magic = d[base + r:base + r + 4]
        kind = next((v for k, v in MAGICS.items() if magic.startswith(k)), None)
        if kind:
            good += 1
            kinds[kind] = kinds.get(kind, 0) + 1
    return good, kinds


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('image', type=Path)
    ap.add_argument('--extract', type=Path, help='write each asset here')
    ap.add_argument('--show', type=int, default=8, help='how many candidates to list')
    a = ap.parse_args()
    d = a.image.read_bytes()
    found = candidates(d)
    if not found:
        print('no candidate table found')
        return 1
    ranked = []
    for base, size, count, rel in found:
        good, kinds = score(d, base, rel)
        ranked.append((good / count, good, count, base, size, kinds))
    ranked.sort(reverse=True)
    print(f'{a.image.name}: {len(found)} structurally valid candidates')
    for frac, good, count, base, size, kinds in ranked[:a.show]:
        print(f'  base 0x{base:06X}  size {size:5d}  entries {count:3d}  '
              f'magic {good}/{count} ({frac:4.0%})  {kinds}')
    best = ranked[0]
    frac, good, count, base, size, kinds = best
    if frac < 0.25:
        print('  best candidate is weakly validated; treating as unresolved')
        return 1
    print(f'\n  best: table at 0x{base:06X}, {count} entries, {good} with asset magics')
    if a.extract:
        a.extract.mkdir(parents=True, exist_ok=True)
        for i in range(count):
            r, s = struct.unpack_from('<II', d, base + 8 + i * 8)
            chunk = d[base + r:base + r + s]
            ext = next((v for k, v in MAGICS.items() if chunk.startswith(k)), 'bin')
            (a.extract / f'{i:03d}_0x{base + r:06X}.{ext}').write_bytes(chunk)
        print(f'  extracted {count} resources to {a.extract}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
