#!/usr/bin/env python3
"""Static census of AX3292 SPR accesses in a firmware dump.

``l.mfspr`` / ``l.mtspr`` take the SPR number as ``rA | K``.  For a constant
SPR the compiler materialises the number into a register first, usually with
``l.ori rX, r0, K`` (or ``l.movhi`` + ``l.ori`` for large values), then emits
the access with a zero immediate:

    l.ori  r4, r0, 0xd800
    l.mfspr r5, r4, 0x0        ; read SPR 0xd800

This scans the image, tracks those small register constants within a basic
block, and reports which SPRs the firmware reads and writes.  It is a sizing
tool for the device backlog, not a proof: register-base accesses whose base
was computed some other way are reported as unresolved.

Usage::

    tools/emu/spr_census.py firmware/original.bin
    tools/emu/spr_census.py firmware/original.bin --range 0x400:0x2600 -v
"""

from __future__ import annotations

import argparse
import collections
from pathlib import Path

from sfr_map import DEFAULT_TABLE, load_table

# --- or1k encoding (from target/or1k/insns.decode) --------------------------
OP_MOVHI = 0x06
OP_ORI = 0x2A
OP_ADDI = 0x27
OP_ANDI = 0x29
OP_XORI = 0x2B
OP_MULI = 0x2C
OP_MFSPR = 0x2D
OP_MTSPR = 0x30
OP_J = 0x00
OP_JAL = 0x01
OP_ADRP = 0x02
OP_BNF = 0x03
OP_BF = 0x04
OP_LWA = 0x0D
OP_JR = 0x11
OP_JALR = 0x12
OP_SLLI = 0x2E
OP_ARITH = 0x38

#: Opcodes whose bits 25..21 are a GPR destination.
DEST_OPS = (
    {OP_ADRP, OP_MOVHI, OP_LWA, OP_ANDI, OP_MULI, OP_SLLI, OP_ARITH}
    | {OP_ADDI, OP_ORI, OP_XORI, OP_MFSPR}
    | set(range(0x21, 0x27))  # l_lwz .. l_lhs
)

#: Opcodes that end a basic block for constant-tracking purposes.
BRANCH_OPS = {OP_J, OP_JAL, OP_BNF, OP_BF, OP_JR, OP_JALR, 0x08, 0x09}

#: or1k ABI: r13..r28 survive a call, r3..r12 and r29..r31 do not (r9 is the
#: link register).  The compiler parks SPR addresses in the callee-saved set.
CALLEE_SAVED = set(range(13, 29))
VOLATILE = (set(range(3, 13)) | {9, 29, 30, 31}) - CALLEE_SAVED

MASK = 0xFFFFFFFF


def sign16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


class Census:
    def __init__(self) -> None:
        self.reads: collections.Counter[int] = collections.Counter()
        self.writes: collections.Counter[int] = collections.Counter()
        self.unresolved = 0
        self.total = 0

    def note(self, spr: int, is_write: bool) -> None:
        (self.writes if is_write else self.reads)[spr] += 1

    def all_sprs(self) -> set[int]:
        return set(self.reads) | set(self.writes)


def scan(data: bytes, census: Census) -> None:
    """Scan one contiguous blob."""
    const: dict[int, int] = {}

    def reg(idx: int) -> int | None:
        return const.get(idx)

    for off in range(0, len(data) - 3, 4):
        w = int.from_bytes(data[off : off + 4], "little")
        op = (w >> 26) & 0x3F
        d = (w >> 21) & 0x1F
        a = (w >> 16) & 0x1F
        k = w & 0xFFFF

        if op == OP_MFSPR:
            base = 0 if a == 0 else reg(a)
            if base is None:
                census.unresolved += 1
            else:
                census.note((base | k) & 0xFFFF, is_write=False)
            const.pop(d, None)
            continue

        if op == OP_MTSPR:
            # k is split: bits 25..21 and 10..0.
            kk = (((w >> 21) & 0x1F) << 11) | (w & 0x7FF)
            base = 0 if a == 0 else reg(a)
            if base is None:
                census.unresolved += 1
            else:
                census.note((base | kk) & 0xFFFF, is_write=True)
            continue

        if op in (OP_J, OP_JR, 0x08, 0x09):
            # Unconditional transfer / mode change: the linear-scan context is gone.
            const.clear()
            continue

        if op in (OP_JAL, OP_JALR):
            # A call clobbers only the volatile registers; callee-saved survive.
            for r in VOLATILE:
                const.pop(r, None)
            continue

        if op in (OP_BNF, OP_BF):
            # Conditional branch: keep the table; a false positive is unlikely
            # within a single function and the extra coverage is worth it.
            continue

        if op == OP_MOVHI:
            const[d] = (k << 16) & MASK
            continue

        if op == OP_ORI:
            if a == 0:
                const[d] = k
            elif reg(a) is not None:
                const[d] = reg(a) | k
            else:
                const.pop(d, None)
            continue

        if op == OP_ADDI:
            if a == 0:
                const[d] = sign16(k) & MASK
            elif reg(a) is not None:
                const[d] = (reg(a) + sign16(k)) & MASK
            else:
                const.pop(d, None)
            continue

        if op in DEST_OPS:
            const.pop(d, None)


def parse_range(text: str) -> tuple[int, int]:
    start, _, end = text.partition(":")
    return int(start, 0), int(end, 0)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", type=Path)
    ap.add_argument(
        "--range",
        dest="ranges",
        action="append",
        type=parse_range,
        help="file offset range START:END (repeatable; default boot+app of a stock image)",
    )
    ap.add_argument("--table", type=Path, default=DEFAULT_TABLE)
    ap.add_argument("-v", "--verbose", action="store_true", help="list every SPR")
    ap.add_argument("--top", type=int, default=40)
    args = ap.parse_args(argv)

    ranges = args.ranges or [(0x400, 0x2600), (0x2600, 0xD3200)]
    data = args.image.read_bytes()
    census = Census()
    for start, end in ranges:
        end = min(end, len(data))
        if start >= end:
            continue
        scan(data[start:end], census)

    table = load_table(args.table)
    census.total = sum(census.reads.values()) + sum(census.writes.values()) + census.unresolved
    print(f"# {args.image}  ranges {[f'{s:#x}:{e:#x}' for s, e in ranges]}")
    print(
        f"# {census.total} SPR accesses, {len(census.all_sprs())} distinct SPRs, "
        f"{census.unresolved} unresolved (non-constant base)"
    )
    print(f"{'SPR':>6}  {'R':>5} {'W':>5}  name")
    ranked = sorted(census.all_sprs(), key=lambda s: -(census.reads[s] + census.writes[s]))
    for spr in ranked:
        if not args.verbose and len(ranked) > args.top and spr not in ranked[: args.top]:
            continue
        name = table.get(spr)
        label = (" / ".join(name.names[:3])) if name else "?"
        print(f"0x{spr:04X}  {census.reads[spr]:5d} {census.writes[spr]:5d}  {label}")
    if args.verbose:
        print(f"# {len(ranked)} distinct SPRs shown")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
