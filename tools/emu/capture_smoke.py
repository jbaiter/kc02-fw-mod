#!/usr/bin/env python3
"""Take a real firmware photo, inspect SD bytes, then view it across cold boots.

Creates a private card under --output; never edits an existing user's card.
Only ADC keys and read-only memory observations drive the guest application.
Requires Pillow for independent validation of the firmware's JRX JPEG packets.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import time

from PIL import Image

from create_sd import create_image
from run import DEFAULT_QEMU
from sd_read import FAT16, jpeg_packets
from smoke import ORIGINAL_SHA256, QMP, read_ppm, wait_for


def photo_files(card):
    with FAT16(card) as sd:
        try:
            entries = sd.entries("PHOTO")
        except KeyError:
            return {}
        return {name: sd.read_file("PHOTO/" + name) for name in entries
                if name.endswith(".JPG")}


def validate(payload):
    packets = jpeg_packets(payload)
    result = {}
    for name, data in packets.items():
        with Image.open(io.BytesIO(data)) as im:
            im.load()  # strict independent entropy validation, no truncated-image option
            assert im.size == (1280, 720), (name, im.size)
            r, g, b = im.getpixel((600, 200))
            assert g > 180 and r < 60 and b < 60, (name, (r, g, b))
            result[name] = {"size": len(data), "dimensions": list(im.size),
                            "sha256": hashlib.sha256(data).hexdigest()}
    return packets, result


def exercise(qemu, firmware, card, directory, *, capture, printer=None,
             no_paper=False, grayscale=False):
    directory.mkdir(parents=True, exist_ok=False)
    before = photo_files(card)
    app = directory / "app.bin"
    app.write_bytes(firmware.read_bytes()[0x2600:0xd3200])
    frame = directory / "screen.ppm"
    sock = directory / "qmp.sock"
    env = {k: v for k, v in os.environ.items() if not k.startswith("AX3292_")}
    env.update(AX3292_FLASH=str(firmware.resolve()), AX3292_SENSOR="1",
               AX3292_SD_IMAGE=str(card), AX3292_JPEG_TRACE="1",
               AX3292_FB=str(frame), AX3292_FB_EVERY="5")
    if printer is not None:
        env["AX3292_PRINTER_DIR"] = str(printer)
        if no_paper:
            env["AX3292_PRINTER_NO_PAPER"] = "1"
    command = [qemu, "-M", "ax3292", "-m", "64M", "-nodefaults",
               "-accel", "tcg,thread=multi", "-display", "none",
               "-monitor", "none", "-serial", "none", "-kernel", str(app),
               "-qmp", f"unix:{sock},server=on,wait=off"]
    qmp = None
    saved = {}
    with (directory / "qemu.log").open("wb") as log:
        proc = subprocess.Popen(command, env=env, stdout=log, stderr=log)
        try:
            wait_for(proc, sock.exists, "QMP socket")
            qmp = QMP(sock)
            scratch = directory / "memory.bin"

            def value(address, size=4):
                return int.from_bytes(qmp.memory(address, size, scratch), "little")

            def bars():
                try:
                    w, h, rgb = read_ppm(frame)
                    r, g, b = rgb[(100 * w + 150) * 3:][:3]
                    return (w, h) == (320, 240) and g > 180 and r < 60 and b < 60
                except (OSError, ValueError):
                    return False

            def save(name):
                target = directory / (name + ".ppm")
                target.write_bytes(frame.read_bytes())
                saved[name] = str(target)

            def menu():
                wait_for(proc, lambda: value(0x020ccd54) == 6 and not bars(), "menu")
                wait_for(proc, lambda: value(0x020ccec4) != 0, "menu JPEGs decoded")
                # Foto retains its menu selection on return; navigate back
                # to Kamera rather than assuming the firmware resets it.
                selection = value(0x020d41d3, 1)
                for target in range(selection - 1, -1, -1):
                    qmp.key("left")
                    wait_for(proc, lambda: value(0x020d41d3, 1) == target,
                             f"return to Kamera ({target})")
                    time.sleep(0.3)

            def viewer(name):
                for selection in range(1, 4):
                    qmp.key("right")
                    wait_for(proc, lambda: value(0x020d41d3, 1) == selection,
                             f"select Foto ({selection})")
                qmp.key("ret")
                wait_for(proc, lambda: value(0x020ccd54) == 4 and bars(), name)
                save(name)
                qmp.key("esc")
                menu()

            menu()
            if grayscale:
                # Einstellungen -> Attribute drucken -> Graustufen, all through
                # real ADC keys. The mode byte below is an observation only.
                for selection in range(1, 6):
                    qmp.key("right")
                    wait_for(proc, lambda: value(0x020d41d3, 1) == selection,
                             f"select Einstellungen ({selection})")
                    time.sleep(0.3)
                qmp.key("ret")
                wait_for(proc, lambda: value(0x020ccd54) == 9, "settings")
                time.sleep(0.3)
                for _ in range(2):
                    qmp.key("right")
                    time.sleep(0.3)
                qmp.key("ret")
                time.sleep(0.3)
                qmp.key("down")  # Punktmatrix -> Graustufen
                time.sleep(0.3)
                save("00-grayscale-setting")
                qmp.key("ret")
                wait_for(proc, lambda: value(0x020d41cc, 1) == 0,
                         "firmware selects grayscale printer mode")
                time.sleep(0.3)
                qmp.key("esc")
                menu()
            if before:
                # Verify the old image is viewable BEFORE taking another photo.
                viewer("01-persisted-viewer")
            if capture:
                qmp.key("ret")
                wait_for(proc, lambda: value(0x020ccd54) == 3 and bars(), "camera")
                save("02-camera")

                qmp.key("ret")

                def written():
                    # Read FAT only while our guest is stopped: not a live
                    # filesystem reader, and never edits guest or card bytes.
                    qmp.call("stop")
                    try:
                        photos = photo_files(card)
                        new = set(photos) - set(before)
                        if len(new) != 1:
                            return False
                        data = photos[next(iter(new))]
                        if len(data) < 12 or data[-12:-4] != b"\xff\xe9\x00\x0aJRX\x00":
                            return False
                        validate(data)
                        return True
                    except (KeyError, ValueError, OSError):
                        return False
                    finally:
                        qmp.call("cont")

                wait_for(proc, written, "firmware saves a complete JRX photo", timeout=30)
                def marker_pixels():
                    rgb = read_ppm(frame)[2]
                    return rgb[180 * 320 * 3:220 * 320 * 3]

                # The available-photo estimate is rounded: it need not change
                # for every JPEG. Require the stopped CSI preview to resume
                # instead of mistaking an old camera frame for completion.
                marker = marker_pixels()
                wait_for(proc, lambda: bars() and marker_pixels() != marker,
                         "shutter completes and CSI preview resumes", timeout=60)
                save("03-after-shutter")
                qmp.key("esc")
                menu()
                viewer("04-new-photo-viewer")
                save("05-back")
        finally:
            if qmp:
                qmp.close()
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
    after = photo_files(card)
    for name, data in before.items():
        assert after[name] == data, f"previous photo changed: {name}"
    assert len(after) == len(before) + int(capture)
    photos = {}
    for name, data in after.items():
        (directory / name).write_bytes(data)  # unmodified on-disk JRX container
        packets, checks = validate(data)
        for kind, packet in packets.items():
            (directory / (name + f".{kind}.jpg")).write_bytes(packet)
        photos[name] = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                        "jpeg_packets": checks}
    text = (directory / "qemu.log").read_text(errors="replace")
    assert "assertion failed" not in text and "ERROR:" not in text
    assert "encode failed" not in text
    report = {"passed": True, "capture": capture, "firmware_sha256": ORIGINAL_SHA256,
              "card": str(card), "command": command, "photos": photos,
              "screenshots": saved}
    (directory / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--qemu", default=DEFAULT_QEMU)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if hashlib.sha256(args.firmware.read_bytes()).hexdigest() != ORIGINAL_SHA256:
        parser.error("specific to firmware/original.bin")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    card = output / "sd.img"
    create_image(card)
    for number, capture in enumerate((True, True, False), 1):
        directory = output / f"run-{number}"
        result = exercise(args.qemu, args.firmware, card, directory, capture=capture)
        print(f"PASS {number}: {'capture + ' if capture else ''}viewer; "
              f"{len(result['photos'])} persistent photo(s) ({directory})", flush=True)


if __name__ == "__main__":
    main()
