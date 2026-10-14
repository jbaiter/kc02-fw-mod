#!/usr/bin/env python3
"""Propagate function names from the original KC02 build to the newer builds.

The builds are relocated, so raw bytes rarely match. The fingerprint therefore masks the
immediate/address fields and keeps only opcode and register fields:

    word & 0xFFFF0000     (16-bit immediate zeroed; branches keep just their opcode)

Each function is fingerprinted by its first N masked words. A name is propagated only when the
fingerprint is unique in the original AND unique in the target build, so an ambiguous match is
dropped rather than guessed. That is deliberately conservative: the same masked-word idea produced
~0.81 mean similarity for unrelated functions in the SDK alignment work, so uniqueness, not a score
threshold, is doing the discrimination here.

Usage:
    propagate_names.py <functions.tsv> <image> <original-program> <target-program> <out.tsv>

The file->CPU mapping base is per build and must match the Ghidra import base:
0x01FFDA00 when the application starts at file 0x2600 (original KC02,
HMOLLIMT Z-12), and 0x01FFDE00 when it starts at file 0x2200 (MYSTILUCK X8D).
When the two programs were imported at different bases, pass both maps.
"""
import argparse
import struct
from collections import defaultdict
from pathlib import Path

MAP = 0x01FFDA00
MASK = 0xFFFF0000
WINDOW = 32          # instructions fingerprinted per function (24-32 measured best)
MIN_SIZE = 24        # bytes; shorter bodies are too generic to identify reliably


def load_functions(path):
    per_program = defaultdict(list)
    for line in Path(path).read_text().splitlines():
        parts = line.split('\t')
        if len(parts) != 4:
            continue
        program, entry, name, size = parts
        per_program[program].append((int(entry.split(':')[-1], 16), name, int(size)))
    return per_program


def fingerprint(image, entry, size, base=MAP):
    """Masked opcode/register words of the function body, bounded by size."""
    n = max(1, min(WINDOW, size // 4))
    words = []
    off = entry - base
    if off < 0 or off + 4 * n > len(image):
        return None
    for i in range(n):
        word = struct.unpack_from('<I', image, off + 4 * i)[0]
        words.append(word & MASK)
    return tuple(words)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('functions', type=Path)
    ap.add_argument('--original-image', type=Path, required=True)
    ap.add_argument('--target-image', type=Path, required=True)
    ap.add_argument('--original', required=True, help='program name of the original')
    ap.add_argument('--target', required=True, help='program name of the target build')
    ap.add_argument('--original-map', type=lambda v: int(v, 0), default=MAP,
                    help='file->CPU base of the original (default 0x01FFDA00)')
    ap.add_argument('--target-map', type=lambda v: int(v, 0), default=MAP,
                    help='file->CPU base of the target (X8D = 0x01FFDE00)')
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()

    per_program = load_functions(a.functions)
    orig_img = a.original_image.read_bytes()
    targ_img = a.target_image.read_bytes()

    def index(image, program, base):
        table = defaultdict(list)
        for entry, name, size in per_program[program]:
            if size < MIN_SIZE:
                continue
            fp = fingerprint(image, entry, size, base)
            if fp is None:
                continue
            table[fp].append((entry, name, size))
        return table

    orig = index(orig_img, a.original, a.original_map)
    targ = index(targ_img, a.target, a.target_map)

    pairs, ambiguous, unmapped = [], 0, 0
    for fp, hits in orig.items():
        if len(hits) != 1:
            continue
        entry, name, _ = hits[0]
        if name.startswith(('FUN_', 'thunk_', 'SUB_')):
            continue                      # nothing worth propagating
        target_hits = targ.get(fp, [])
        if len(target_hits) == 1:
            pairs.append((entry, name, target_hits[0][0], target_hits[0][2]))
        elif len(target_hits) > 1:
            ambiguous += 1
        else:
            unmapped += 1

    a.out.parent.mkdir(parents=True, exist_ok=True)
    with a.out.open('w') as fh:
        fh.write('# original_addr\ttarget_addr\tname\ttarget_size\n')
        for oaddr, name, taddr, tsize in sorted(pairs, key=lambda r: r[2]):
            fh.write(f'0x{oaddr:08X}\t0x{taddr:08X}\t{name}\t{tsize}\n')
    print(f'{a.target}: {len(pairs)} names propagated')
    print(f'   original fingerprinted functions : {len(orig)}')
    print(f'   target fingerprinted functions   : {len(targ)}')
    print(f'   skipped, name absent from target  : {unmapped}')
    print(f'   skipped, ambiguous in target      : {ambiguous}')
    print(f'   written to {a.out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
