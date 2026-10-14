#!/usr/bin/env python3
"""Run a dumped AX3292 firmware image on the patched QEMU ``ax3292`` machine.

The flash image is not a memory image: the application lives at a build-specific
file offset and expects to be at SDRAM ``0x02000000``, and the boot code enters
it at a fixed address.  This slices the application out of the dump and runs it.

    tools/emu/run.py firmware/original.bin
    tools/emu/run.py firmware/original.bin --decode    # histogram, with names

The machine and its SPR trace come from
``tools/emu/qemu/0001-ax3292-machine.patch``; see ``tools/emu/qemu/README.md``.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
import collections
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from sfr_map import DEFAULT_TABLE, load_table  # noqa: E402

DEFAULT_QEMU = os.environ.get(
    "QEMU_OR1K", str(Path.home() / ".src/qemu-11.1.2/build/qemu-system-or1k")
)

#: Boot entry the boot code calls (see the trace of file 0x520..0x54c).
DEFAULT_ENTRY = 0x0200046C

#: Per-build application extent inside the flash image.
#:   <name>: (file offset of the app image, app end == start of the asset table)
LAYOUTS = {
    "original": (0x2600, 0x0D3200),
    "x8d": (0x2200, 0x07FA00),        # MYSTILUCK X8D
    "z12": (0x2600, None),            # HMOLLIMT Z-12 (asset table unresolved)
}


def guess_layout(image: Path) -> tuple[int, int | None]:
    """Pick a layout by matching the dump against the known builds."""
    import hashlib

    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    known = {
        "218c6cdf": "original",
        "3ed8c0f1": "x8d",
        "10da5dbf": "z12",
    }
    for prefix, name in known.items():
        if digest.startswith(prefix):
            return LAYOUTS[name]
    return LAYOUTS["original"]


def find_asset_table(image: Path) -> int | None:
    """Locate the SFAT asset table (the application's end) with tools/sfat."""
    tool = HERE.parent / "sfat" / "find_sfat.py"
    if not tool.exists():
        return None
    try:
        proc = subprocess.run(
            [sys.executable, str(tool), str(image)],
            capture_output=True, text=True, timeout=300,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    match = re.search(r"best: table at 0x([0-9A-Fa-f]+)", proc.stdout)
    return int(match.group(1), 16) if match else None


def decode(log: Path, table: dict) -> None:
    reads = collections.Counter()
    writes = collections.Counter()
    for line in log.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) < 4 or parts[0] != "spr":
            continue
        spr = int(parts[2], 16)
        (writes if parts[1] == "W" else reads)[spr] += 1
    total = sum(reads.values()) + sum(writes.values())
    print(f"# {total} SPR accesses, {len(set(reads) | set(writes))} distinct")
    print(f"{'SPR':>6}  {'R':>7} {'W':>7}  name")
    for spr in sorted(set(reads) | set(writes), key=lambda s: -(reads[s] + writes[s]))[:40]:
        entry = table.get(spr)
        label = " / ".join(entry.names[:2]) if entry else "?"
        print(f"0x{spr:04X}  {reads[spr]:7d} {writes[spr]:7d}  {label}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", type=Path, help="firmware dump (4 MiB)")
    ap.add_argument("--qemu", default=DEFAULT_QEMU, help="path to qemu-system-or1k")
    ap.add_argument("--app-offset", type=lambda s: int(s, 0), help="app file offset (default: detected)")
    ap.add_argument("--app-end", type=lambda s: int(s, 0), help="app end file offset (default: detected)")
    ap.add_argument("--entry", type=lambda s: int(s, 0), default=DEFAULT_ENTRY)
    ap.add_argument("--trace-limit", type=int, default=300000)
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--log", type=Path, help="SPR trace output (default: alongside the image)")
    ap.add_argument("--decode", action="store_true", help="summarise an existing log instead of running")
    ap.add_argument("--table", type=Path, default=DEFAULT_TABLE)
    args = ap.parse_args(argv)

    log = args.log or args.image.with_suffix(args.image.suffix + ".spr.log")

    if args.decode:
        decode(log, load_table(args.table))
        return 0

    if not Path(args.qemu).exists():
        print(f"error: {args.qemu} not found; see tools/emu/qemu/README.md", file=sys.stderr)
        return 2

    app_offset, app_end = guess_layout(args.image)
    if args.app_offset is not None:
        app_offset = args.app_offset
    if args.app_end is not None:
        app_end = args.app_end
    elif app_end is None:
        app_end = find_asset_table(args.image)
    if app_end is None:
        print("error: could not find the application end; pass --app-end", file=sys.stderr)
        return 2

    data = args.image.read_bytes()
    app = data[app_offset:app_end]
    print(f"app: file {app_offset:#x}..{app_end:#x} ({len(app)} bytes) -> 0x02000000, "
          f"entry {args.entry:#x}")

    with tempfile.NamedTemporaryFile(prefix="ax3292-app-", suffix=".bin", delete=False) as tmp:
        tmp.write(app)
        app_path = tmp.name

    env = dict(os.environ)
    env["AX3292_ENTRY"] = hex(args.entry)
    env["AX3292_SPR_TRACE_LIMIT"] = str(args.trace_limit)
    env["AX3292_FLASH"] = str(args.image)

    cmd = [
        args.qemu, "-M", "ax3292", "-m", "64M",
        "-display", "none", "-monitor", "none", "-serial", "none",
        "-kernel", app_path,
    ]
    uart_log = Path(str(log) + ".uart")
    print("$ " + " ".join(cmd))
    try:
        with log.open("wb") as errf, uart_log.open("wb") as outf:
            proc = subprocess.run(cmd, env=env, stdout=outf, stderr=errf,
                                  timeout=args.timeout)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        rc = 124
    finally:
        os.unlink(app_path)

    print(f"qemu exit {rc}; trace -> {log}")
    if uart_log.exists() and uart_log.stat().st_size:
        text = uart_log.read_text(errors="replace")
        print(f"uart -> {uart_log} ({len(text)} bytes)")
        for line in text.splitlines()[:40]:
            print("  | " + line)
    decode(log, load_table(args.table))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
