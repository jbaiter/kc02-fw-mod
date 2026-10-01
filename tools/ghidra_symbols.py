#!/usr/bin/env python3
"""ghidra_symbols.py - apply / verify the KC02 Ghidra annotation manifest.

``tools/ghidra_symbols.tsv`` is the replayable single source of truth for the
KC02 UI/USB/key-input symbols.  This wrapper replays it into any Ghidra project
that already holds the imported firmware, using ``tools/ApplyKC02Symbols.java``
(annotations only: function renames, labels, plate/EOL comments - no byte
patches, no memory blocks, no analyzer runs).

Typical use - rebuild the whole symbol set in a FRESH cache:

    python3 tools/ghidra_symbols.py --dry-run
    python3 tools/ghidra_symbols.py --apply \\
        --project-dir /home/jbaiter/pi-ghidra-cache-kc02-fresh \\
        --program d837019dd7de37ec.bin

The same command against the main analysis cache works too, but the main cache
is normally annotated in place with the `ghidra` MCP action, which does not
need Ghidra to be launched a second time:

    python3 tools/ghidra_symbols.py --apply --project-dir \\
        /home/jbaiter/pi-ghidra-cache-kc02-or1k --program d837019dd7de37ec.bin

Safety: this tool never touches the firmware image and never talks to a USB
device.  It only runs Ghidra against an existing project.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from typing import List, Sequence, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_TSV = os.path.join(HERE, "ghidra_symbols.tsv")
DEFAULT_GHIDRA_HOME = os.environ.get(
    "GHIDRA_HOME", "/home/jbaiter/.ghidra/ghidra_12.1.4_PUBLIC"
)
DEFAULT_PROJECT_DIR = "/home/jbaiter/pi-ghidra-cache-kc02-or1k"
DEFAULT_PROGRAM = "d837019dd7de37ec.bin"

KINDS = ("fun", "label", "comment")
COMMENT_TYPES = ("EOL", "PRE", "PLATE", "REPEATABLE")


def parse_tsv(path: str) -> Tuple[List[Tuple[str, str, str, str]], List[str]]:
    """Return (records, problems).  A record is (addr_hex, kind, name, comment)."""
    records: List[Tuple[str, str, str, str]] = []
    problems: List[str] = []
    with open(path, "r", encoding="utf-8") as fh:
        for lineno, raw in enumerate(fh, 1):
            line = raw.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 4:
                problems.append(f"line {lineno}: expected 4 TAB fields, got {len(parts)}")
                continue
            addr, kind, name = (p.strip() for p in parts[:3])
            comment = parts[3]
            try:
                int(addr, 16)
            except ValueError:
                problems.append(f"line {lineno}: bad address {addr!r}")
                continue
            if kind not in KINDS:
                problems.append(f"line {lineno}: bad kind {kind!r}")
                continue
            if kind == "comment" and name not in COMMENT_TYPES:
                problems.append(
                    f"line {lineno}: comment type {name!r} not one of {COMMENT_TYPES}"
                )
                continue
            records.append((addr, kind, name, comment))
    return records, problems


def verify(path: str) -> int:
    records, problems = parse_tsv(path)
    counts = {k: sum(1 for r in records if r[1] == k) for k in KINDS}
    addrs = [r[0] for r in records]
    dupes = sorted({a for a in addrs if addrs.count(a) > 1})
    print(f"manifest       : {path}")
    print(f"records        : {len(records)}  "
          f"(fun={counts['fun']} label={counts['label']} comment={counts['comment']})")
    print(f"distinct addrs : {len(set(addrs))}")
    if dupes:
        print(f"duplicate addrs: {', '.join(dupes)}")
    inferred = sum(1 for _a, _k, _n, c in records if "INFERRED" in c)
    confirmed = sum(1 for _a, _k, _n, c in records if "CONFIRMED" in c)
    tagged = sum(1 for _a, _k, _n, c in records
                 if "CONFIRMED" in c or "INFERRED" in c)
    print(f"evidence tags  : records mentioning CONFIRMED={confirmed}, "
          f"INFERRED={inferred}; untagged={len(records) - tagged}")
    for p in problems:
        print(f"PROBLEM: {p}", file=sys.stderr)
    if problems:
        return 1
    print("manifest OK")
    return 0


def apply_manifest(args: argparse.Namespace) -> int:
    records, problems = parse_tsv(args.tsv)
    if problems:
        for p in problems:
            print(f"refusing to apply, malformed manifest: {p}", file=sys.stderr)
        return 1
    launcher = os.path.join(args.ghidra_home, "support", "analyzeHeadless")
    if not os.path.isfile(launcher):
        raise SystemExit(f"analyzeHeadless not found at {launcher}")
    script = os.path.join(HERE, "ApplyKC02Symbols.java")
    if not os.path.isfile(script):
        raise SystemExit(f"script not found: {script}")

    cmd = [
        launcher, args.project_dir, args.project_name,
        "-process", args.program,
        "-noanalysis",
        "-scriptPath", HERE,
        "-postScript", "ApplyKC02Symbols.java", os.path.abspath(args.tsv),
        "-max-cpu", "1",
    ]
    print("running:", " ".join(cmd))
    if args.dry_run:
        print(f"(dry-run) would apply {len(records)} annotation records "
              f"to {args.project_dir}")
        return 0
    # Capture the output so a script error cannot masquerade as success (the
    # Java script prints its own applied/skipped/malformed counters).
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = proc.stdout.decode("utf-8", "replace")
    sys.stdout.write(output)
    if proc.returncode != 0:
        print(f"analyzeHeadless exited {proc.returncode}", file=sys.stderr)
        return proc.returncode
    if "SCRIPT ERROR" in output or "error: cannot find symbol" in output:
        print("ghidra_symbols: the apply script FAILED to run (see the log "
              "above) - nothing was annotated", file=sys.stderr)
        return 1
    summary = [ln for ln in output.splitlines()
               if "ApplyKC02Symbols: " in ln and "applied," in ln]
    if not summary:
        print("ghidra_symbols: no summary line from ApplyKC02Symbols - the "
              "manifest was NOT applied", file=sys.stderr)
        return 1
    tail = summary[-1].split("ApplyKC02Symbols: ", 1)[1]
    applied, skipped, malformed = (
        int(part.strip().split()[0])
        for part in tail.split(",")
    )
    if malformed or skipped or applied != len(records):
        print(f"ghidra_symbols: applied={applied} skipped={skipped} "
              f"malformed={malformed} for {len(records)} records - review the log",
              file=sys.stderr)
        return 1
    print(f"applied {applied} annotation records to {args.project_dir}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--tsv", default=DEFAULT_TSV, help="manifest path")
    p.add_argument("--verify", action="store_true",
                   help="only parse/validate the manifest (no Ghidra)")
    p.add_argument("--apply", action="store_true",
                   help="replay the manifest into the Ghidra project")
    p.add_argument("--dry-run", action="store_true",
                   help="with --apply: print the command, launch nothing")
    p.add_argument("--project-dir", default=DEFAULT_PROJECT_DIR)
    p.add_argument("--project-name", default="pi-ghidra")
    p.add_argument("--program", default=DEFAULT_PROGRAM)
    p.add_argument("--ghidra-home", default=DEFAULT_GHIDRA_HOME)
    return p


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.verify or not args.apply:
        rc = verify(args.tsv)
        if not args.apply:
            return rc
        if rc:
            return rc
    return apply_manifest(args)


if __name__ == "__main__":
    sys.exit(main())
