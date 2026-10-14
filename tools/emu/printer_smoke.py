#!/usr/bin/env python3
"""Capture and print through real firmware keys in dither, gray and no-paper modes.

Owns a fresh card and three cold QEMU runs. No application-state injection.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

from PIL import Image

from capture_smoke import exercise
from create_sd import create_image
from run import DEFAULT_QEMU
from smoke import ORIGINAL_SHA256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--qemu", default=DEFAULT_QEMU)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.firmware.read_bytes()).hexdigest() != ORIGINAL_SHA256:
        parser.error("specific to firmware/original.bin")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    card = output / "sd.img"
    create_image(card)
    results = {}
    for name, gray, empty in [("dither", False, False), ("grayscale", True, False),
                              ("no-paper", False, True)]:
        directory = output / name
        printer = directory / "prints"
        exercise(args.qemu, args.firmware, card, directory, capture=True,
                 printer=printer, no_paper=empty, grayscale=gray)
        pages = sorted(printer.glob("*.png"))
        assert len(pages) == (0 if empty else 1), pages
        if pages:
            with Image.open(pages[0]) as im:
                im.load()
                assert im.mode == "L" and im.width == 384
                assert 672 <= im.height <= 760, im.size
                samples = im.crop((1, 2, 384, 670))
                levels = {value for _, value in samples.getcolors(65536)}
                if gray:
                    assert len(levels) > 8, levels
                else:
                    assert levels == {0, 255}, levels
                # The post-print feed is blank paper, not appended image rows.
                assert im.crop((0, im.height - 60, 384, im.height)).getextrema() == (255, 255)
                fields = re.search(r"clocks=(\d+) latches=(\d+) strobes=(\d+) "
                                   r"max_per_row=(\d+) steps=(\d+) bad_steps=(\d+)",
                                   (directory / "qemu.log").read_text(errors="replace"))
                assert fields is not None
                metrics = dict(zip(("clocks", "latches", "strobes", "max_per_row",
                                    "steps", "bad_steps"), map(int, fields.groups())))
                assert metrics["clocks"] == metrics["latches"] * 383
                assert metrics["strobes"] == metrics["latches"]
                assert metrics["max_per_row"] == (32 if gray else 4)
                assert metrics["bad_steps"] == 0
                results[name] = {"path": str(pages[0]), "dimensions": list(im.size),
                                 "levels": sorted(levels), "gpio": metrics,
                                 "sha256": hashlib.sha256(pages[0].read_bytes()).hexdigest()}
        else:
            results[name] = {"pages": 0, "passed": True}
        print(f"PASS {name}: {len(pages)} thermal-head PNG(s); capture/viewer/back", flush=True)
    (output / "printer-report.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
