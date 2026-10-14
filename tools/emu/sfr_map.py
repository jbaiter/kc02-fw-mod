#!/usr/bin/env python3
"""AX3292 special-purpose-register (SPR) map.

`l.mfspr` / `l.mtspr` encode a 16-bit SPR number.  The operand is the OR of a
base register and an immediate field, so a statically encoded access is
``l.mfspr rD, r0, K`` with SPR number ``K``.

The vendor compiler turns a ``__sfr`` object at byte address ``A`` into an
access with SPR number ``A`` verbatim (verified by compiling with the
reconstructed toolchain: address ``0x9244`` -> ``l.mtspr r0, r3, 0x9244``).
So the vendor SDK header ``ax329x.h`` doubles as the SPR number table:

    SPR = (group << 11) | (index << 2)

Two caveats, both encoded below:

* **Group 0 ("SYS") exception registers.**  The header is wrong here.  The
  vendor's own exception entry (``ax32xx/reset.S``) uses ``l.mfspr r9,r0,32``
  and comments it "save EPCR", and ``l.mfspr r9,r0,64`` "save ESR".  The stock
  image contains exactly those accesses.  The header instead labels ``0x40``
  EPCR, ``0x80`` EEAR and ``0xC0`` ESR.  CORRECTIONS fixes this.

* **Hardware-verified values.**  A probe run on a real device confirmed
  ``SPR_VR = 0x0000`` (``0x65803200``), ``SPR_SR = 0x0018``, ``SPR_PICVR =
  0x480c`` and ``SPR_ICCR = 0x2000``; ``0x11`` is undefined on this core (it is
  the or1k-standard SR number, which this chip does not use).

Usage::

    tools/emu/sfr_map.py --sdk /path/to/ax329x.h --emit tsv
    tools/emu/sfr_map.py --lookup 0x9244
    tools/emu/sfr_map.py --group 18
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_TABLE = HERE / "sfr_map.tsv"

#: Number of bits the group number is shifted by (matches ``MAX_SPRS_PER_GRP_BITS``).
MAX_SPRS_PER_GRP_BITS = 11

#: group name -> base, derived from ``SPRGROUP_*`` defines.
_GROUP_RE = re.compile(
    r"#define\s+(SPRGROUP_[A-Za-z0-9_]+)\s+\(\s*(\d+)\s*<<\s*MAX_SPRS_PER_GRP_BITS\s*\)"
)
#: register name -> (group name, index).
_REG_RE = re.compile(
    r"#define\s+([A-Za-z0-9_]+)\s+SFR_AT\s+\(\s*"
    r"(SPRGROUP_[A-Za-z0-9_]+)\s*\+\s*\(\s*(0x[0-9A-Fa-f]+|\d+)\s*<<\s*2\s*\)\s*\)"
)

#: Authoritative overrides for the group-0 exception registers.
#: (spr, name, confidence, evidence)
CORRECTIONS: list[tuple[int, str, str, str]] = [
    (0x20, "SPR_EPCR", "high", "vendor reset.S comment + stock image exception entry"),
    (0x40, "SPR_ESR", "high", "vendor reset.S comment + stock image exception entry"),
]

#: Entries the vendor header gives but we could not confirm; kept for reference.
SUSPECT: dict[int, str] = {
    0x80: "SPR_EEAR?",
    0xC0: "SPR_ESR?",
}

#: Hardware-verified (spr, name, value).
HARDWARE_VERIFIED: dict[int, tuple[str, int]] = {
    0x0000: ("SPR_VR", 0x65803200),
    0x0018: ("SPR_SR", 0x0000C486),
    0x480C: ("SPR_PICVR", 0x00000000),
    0x2000: ("SPR_ICCR", 0x00000000),
}

#: Group-0 registers the or1k spec defines that this core does *not* use.
UNDEFINED_ON_CORE = {
    0x11: "or1k-standard SR number; probe showed 0x11 is undefined here",
}


class SprEntry:
    __slots__ = ("spr", "names", "group", "confidence", "evidence")

    def __init__(self, spr: int, group: int, confidence: str = "header", evidence: str = ""):
        self.spr = spr
        self.names: list[str] = []
        self.group = group
        self.confidence = confidence
        self.evidence = evidence

    @property
    def name(self) -> str:
        return self.names[0] if self.names else ""

    def as_row(self) -> list[str]:
        return [self.name, f"0x{self.spr:04X}", str(self.group), self.confidence]


def parse_sdk(path: Path) -> dict[int, SprEntry]:
    """Parse ``ax329x.h`` into ``spr -> SprEntry``."""
    text = path.read_text(errors="replace")
    # ``SPRGROUP_X`` defines are ``(n << MAX_SPRS_PER_GRP_BITS)``; capture n and shift.
    groups = {name: int(base) << MAX_SPRS_PER_GRP_BITS for name, base in _GROUP_RE.findall(text)}
    groups.setdefault("SPRGROUP_SYS", 0)

    entries: dict[int, SprEntry] = {}
    for name, group_name, index in _REG_RE.findall(text):
        base = groups.get(group_name)
        if base is None:
            continue
        spr = base + (int(index, 0) << 2)
        entry = entries.get(spr)
        if entry is None:
            entry = entries[spr] = SprEntry(spr, base >> 11)
        if name not in entry.names:
            entry.names.append(name)

    for spr, name, confidence, evidence in CORRECTIONS:
        entry = entries.get(spr)
        if entry is None:
            entry = entries[spr] = SprEntry(spr, 0)
        # Drop the header's wrong label for this number, then install the fix.
        entry.names = [n for n in entry.names if n not in (name,)]
        entry.names.insert(0, name)
        entry.confidence = confidence
        entry.evidence = evidence

    for spr, label in SUSPECT.items():
        entry = entries.get(spr)
        if entry is not None:
            entry.confidence = "suspect"
            entry.evidence = "header only; contradicted by the vendor reset.S"

    for spr, (name, value) in HARDWARE_VERIFIED.items():
        entry = entries.get(spr)
        if entry is None:
            entry = entries[spr] = SprEntry(spr, spr >> 11)
        if name not in entry.names:
            entry.names.insert(0, name)
        entry.confidence = "hardware"
        entry.evidence = f"device probe = 0x{value:08X}"

    return entries


def load_table(path: Path = DEFAULT_TABLE) -> dict[int, SprEntry]:
    """Load the checked-in TSV (name, spr, group, confidence)."""
    entries: dict[int, SprEntry] = {}
    for line in path.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        name, spr_hex, group = parts[0], parts[1], parts[2]
        confidence = parts[3] if len(parts) > 3 else "header"
        spr = int(spr_hex, 16)
        entry = entries.setdefault(spr, SprEntry(spr, int(group), confidence))
        if name and name not in entry.names:
            entry.names.append(name)
    return entries


def emit(entries: dict[int, SprEntry], fmt: str) -> str:
    if fmt == "json":
        return json.dumps(
            [
                {
                    "spr": f"0x{e.spr:04X}",
                    "names": e.names,
                    "group": e.group,
                    "confidence": e.confidence,
                    "evidence": e.evidence,
                }
                for e in sorted(entries.values(), key=lambda e: e.spr)
            ],
            indent=1,
        )
    lines = []
    for e in sorted(entries.values(), key=lambda e: e.spr):
        lines.append("\t".join(e.as_row()))
    return "\n".join(lines) + "\n"


def describe(entries: dict[int, SprEntry], spr: int) -> str:
    entry = entries.get(spr)
    if entry is None:
        note = UNDEFINED_ON_CORE.get(spr)
        if note:
            return f"0x{spr:04X}: undefined on this core ({note})"
        return f"0x{spr:04X}: unknown"
    out = f"0x{spr:04X}: {' / '.join(entry.names)}  (group {entry.group}, {entry.confidence})"
    if entry.evidence:
        out += f"  [{entry.evidence}]"
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdk", type=Path, help="path to the vendor ax329x.h")
    ap.add_argument("--emit", choices=["tsv", "json"], help="write the map to stdout")
    ap.add_argument("--lookup", help="describe one SPR number (hex or decimal)")
    ap.add_argument("--group", type=int, help="list the registers in one group")
    ap.add_argument("--table", type=Path, default=DEFAULT_TABLE, help="checked-in TSV to read")
    args = ap.parse_args(argv)

    if args.sdk:
        entries = parse_sdk(args.sdk)
    else:
        entries = load_table(args.table)

    if args.emit:
        sys.stdout.write(emit(entries, args.emit))
        return 0

    if args.lookup is not None:
        spr = int(args.lookup, 0)
        print(describe(entries, spr))
        return 0

    if args.group is not None:
        for e in sorted(entries.values(), key=lambda e: e.spr):
            if e.group == args.group:
                print(describe(entries, e.spr))
        return 0

    print(f"{len(entries)} SPRs, {sum(1 for e in entries.values() if e.confidence == 'hardware')} hardware-verified")
    for e in sorted(entries.values(), key=lambda e: e.spr):
        if e.confidence != "header":
            print("  " + describe(entries, e.spr))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
