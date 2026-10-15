#!/usr/bin/env python3
"""Run a dumped AX3292 firmware image on the patched QEMU ``ax3292`` machine.

The flash image is not a memory image: the application lives at a build-specific
file offset and expects to be at SDRAM ``0x02000000``, and the boot code enters
it at a fixed address.  This slices the application out of the dump and runs it.

    tools/emu/run.py firmware/original.bin
    tools/emu/run.py firmware/original.bin --decode    # histogram, with names
    tools/emu/run.py firmware/original.bin --interactive  # SDL window + fake sensor

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
from profiles import PROFILES, detect, machine_args  # noqa: E402

DEFAULT_QEMU = os.environ.get(
    "QEMU_OR1K", str(Path.home() / ".src/qemu-11.1.2/build/qemu-system-or1k")
)

#: Boot entry the boot code calls (see the trace of file 0x520..0x54c).
DEFAULT_ENTRY = PROFILES["original"].entry

# Retain these importable names; profiles are the boot-layout source of truth.
LAYOUTS = {name: (p.app_offset, p.app_end) for name, p in PROFILES.items()}


def guess_layout(image: Path) -> tuple[int, int | None]:
    """Pick a layout by matching the complete dump hash."""
    profile = detect(image)
    return profile.app_offset, profile.app_end


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
    ap.add_argument("--profile", choices=["auto", *PROFILES], default="auto",
                    help="boot handoff profile; select explicitly for modified dumps")
    ap.add_argument("--entry", type=lambda s: int(s, 0), help="override detected application entry")
    ap.add_argument("--trace-limit", type=int, default=300000)
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--interactive", action="store_true",
                    help="SDL window with keypad input; run until the window closes")
    ap.add_argument("--display", help="QEMU display backend (default: sdl if interactive, else none)")
    ap.add_argument("--sensor", action=argparse.BooleanOptionalAction, default=None,
                    help="fake H63P sensor with moving colour bars (default: on if interactive)")
    ap.add_argument("--trace", action="store_true", help="enable SPR tracing in interactive mode")
    ap.add_argument("--qmp", help="optional QEMU QMP address, e.g. unix:/tmp/kc02.qmp,server=on,wait=off")
    ap.add_argument("--gdb", type=int, metavar="PORT", help="GDB server on localhost (guest runs until attached)")
    ap.add_argument("--sd-card", type=Path, help="writable raw SD image (create one with tools/emu/create_sd.py)")
    ap.add_argument("--printer-output", type=Path, metavar="DIR",
                    help="emulate paper/cover present and save thermal-head PNGs in DIR")
    ap.add_argument("--printer-no-paper", action="store_true",
                    help="with --printer-output, report an empty/open printer")
    ap.add_argument("--log", type=Path, help="SPR trace output (default: alongside the image)")
    ap.add_argument("--decode", action="store_true", help="summarise an existing log instead of running")
    ap.add_argument("--table", type=Path, default=DEFAULT_TABLE)
    args = ap.parse_args(argv)
    if args.printer_no_paper and args.printer_output is None:
        ap.error("--printer-no-paper requires --printer-output")
    if args.gdb is not None and not 1 <= args.gdb <= 65535:
        ap.error("--gdb port must be between 1 and 65535")
    if args.sd_card is not None and not args.sd_card.is_file():
        ap.error("--sd-card must name an existing raw image; use tools/emu/create_sd.py")

    log = args.log or args.image.with_suffix(args.image.suffix + ".spr.log")

    if args.decode:
        decode(log, load_table(args.table))
        return 0

    if not Path(args.qemu).exists():
        print(f"error: {args.qemu} not found; see tools/emu/qemu/README.md", file=sys.stderr)
        return 2

    profile = detect(args.image) if args.profile == "auto" else PROFILES[args.profile]
    entry = args.entry if args.entry is not None else profile.entry
    app_offset, app_end = profile.app_offset, profile.app_end
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
          f"entry {entry:#x} ({profile.name})")

    with tempfile.NamedTemporaryFile(prefix="ax3292-app-", suffix=".bin", delete=False) as tmp:
        tmp.write(app)
        app_path = tmp.name

    env = dict(os.environ)
    # Diagnostics only: the gate makes every AX3292_TRACE_*/AX3292_DUMP_* inert.
    env["AX3292_DEBUG"] = "1"
    env["AX3292_TRACE_SPR_LIMIT"] = str(args.trace_limit)
    properties = {"flash": str(args.image.resolve()), "entry": hex(entry)}
    if args.sd_card is not None:
        properties["sd-image"] = str(args.sd_card.resolve())
        print(f"SD: {args.sd_card.resolve()} (writable)", flush=True)
    if args.printer_output is not None:
        properties["printer-dir"] = str(args.printer_output.resolve())
        properties["printer-no-paper"] = "on" if args.printer_no_paper else "off"
        print(f"Printer: {args.printer_output.resolve()} (thermal-head PNGs)", flush=True)
    env["AX3292_TRACE_SPR"] = "1" if args.trace or not args.interactive else "0"
    sensor_enabled = args.sensor if args.sensor is not None else args.interactive
    properties["sensor"] = "on" if sensor_enabled else "off"

    cmd = [
        args.qemu, *machine_args(profile.options(), properties), "-m", "64M", "-nodefaults",
        "-accel", "tcg,thread=multi",
        "-name", f"AX3292 - {profile.name} firmware",
        "-display", args.display or ("sdl" if args.interactive else "none"),
        "-monitor", "none", "-serial", "none",
        "-kernel", app_path,
    ]
    if args.qmp:
        cmd.extend(["-qmp", args.qmp])
    if args.gdb is not None:
        cmd.extend(["-gdb", f"tcp:127.0.0.1:{args.gdb}"])
    uart_log = Path(str(log) + ".uart")
    print("$ " + " ".join(cmd), flush=True)
    if args.interactive:
        print("Arrow keys: navigate; Enter: OK; Escape: back/power. Use short presses.\n"
              "Close the QEMU window or press Ctrl-C here to quit.", flush=True)
        if profile.name != "original":
            print("Space: shutter; P/F/Tab/M: native extra keys (see tools/emu/README.md).",
                  flush=True)
        if profile.dual_sensor:
            print("In camera view, Tab (native Right) switches front/selfie sensors.", flush=True)
    try:
        with log.open("wb") as errf, uart_log.open("wb") as outf:
            proc = subprocess.run(cmd, env=env, stdout=outf, stderr=errf,
                                  timeout=None if args.interactive else args.timeout)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        rc = 124
    except KeyboardInterrupt:
        rc = 130
    finally:
        os.unlink(app_path)

    print(f"qemu exit {rc}; trace -> {log}")
    if rc not in (0, 124, 130):
        # Startup failures (locked card, missing SDL display, etc.) used to be
        # hidden in the trace file behind an unhelpful empty SPR histogram.
        with log.open("rb") as errors:
            errors.seek(max(0, log.stat().st_size - 8192))
            text = errors.read().decode(errors="replace")
        for line in text.splitlines()[-40:]:
            print(line, file=sys.stderr)
    if uart_log.exists() and uart_log.stat().st_size:
        text = uart_log.read_text(errors="replace")
        print(f"uart -> {uart_log} ({len(text)} bytes)")
        for line in text.splitlines()[:40]:
            print("  | " + line)
    if args.trace or not args.interactive:
        decode(log, load_table(args.table))
    return rc if rc >= 0 else 128 - rc


if __name__ == "__main__":
    raise SystemExit(main())
