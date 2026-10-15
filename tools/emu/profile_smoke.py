#!/usr/bin/env python3
"""Cold-boot known alternate firmwares using native ADC input and private SD.

All application memory accesses are read-only observations. Optional capture
checks require saved JPEGs and return to menu; printing is not tested.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

from create_sd import create_image
from profiles import detect, machine_args
from run import DEFAULT_QEMU
from sd_read import FAT16
from smoke import QMP, read_ppm, wait_for

# From each build's mode-request function, not assumptions about shared RAM.
OBSERVATIONS = {
    "x8d": {"mode": 0x0208127c},
    "z12": {"mode": 0x0209c354, "camera": 0x0209e2a8},
}


def exercise(image: Path, directory: Path, qemu: str = DEFAULT_QEMU,
             selfie: bool = False, capture: bool = False,
             printer: bool = False) -> dict:
    profile = detect(image)
    if profile.name not in OBSERVATIONS:
        raise ValueError("use smoke.py for the original firmware")
    if selfie and not profile.dual_sensor:
        raise ValueError("selfie check requires the Z-12 profile")
    directory.mkdir(parents=True, exist_ok=False)
    directory = directory.resolve()
    card = directory / "sd.img"
    create_image(card, test_images=True)
    with FAT16(card) as sd:
        before = {name: hashlib.sha256(sd.read_file("PHOTO/" + name)).hexdigest()
                  for name in sd.entries("PHOTO")}
    app = directory / "app.bin"
    app.write_bytes(image.read_bytes()[profile.app_offset:profile.app_end])
    frame = directory / "screen.ppm"
    sock = directory / "qmp.sock"
    env = {key: value for key, value in os.environ.items() if not key.startswith("AX3292_")}
    env.update(AX3292_DEBUG="1", AX3292_DUMP_FB=str(frame),
               AX3292_DUMP_FB_EVERY="5", AX3292_DUMP_SFR=str(directory / "sfr.bin"),
               AX3292_TRACE_JPEG="1", AX3292_TRACE_KEY="1")
    properties = {"flash": str(image.resolve()), "sensor": "on",
                  "sd-image": str(card)}
    if printer:
        properties["printer-dir"] = str(directory / "prints")
    command = [qemu, *machine_args(profile.options(), properties), "-m", "64M",
               "-nodefaults",
               "-accel", "tcg,thread=multi", "-display", "none", "-monitor", "none",
               "-serial", "none", "-kernel", str(app), "-qmp",
               f"unix:{sock},server=on,wait=off"]
    captures = {}
    report = {"profile": profile.name, "firmware_sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
              "card": str(card), "captures": captures, "selfie": "not requested",
              "printing": "not verified", "capture": "not requested"}
    qmp = None
    with (directory / "qemu.log").open("wb") as log:
        proc = subprocess.Popen(command, env=env, stdout=log, stderr=log)
        try:
            wait_for(proc, sock.exists, "QMP socket")
            qmp = QMP(sock)
            scratch = directory / "observation.bin"

            def value(address, size=4):
                return int.from_bytes(qmp.memory(address, size, scratch), "little")

            def mode():
                return value(OBSERVATIONS[profile.name]["mode"])

            def picture_ready():
                try:
                    w, h, pixels = read_ppm(frame)
                    return (w, h) == (320, 240) and len(set(pixels)) > 32
                except (OSError, ValueError):
                    return False

            def save(name):
                wait_for(proc, picture_ready, name)
                data = frame.read_bytes()
                target = directory / (name + ".ppm")
                target.write_bytes(data)
                captures[name] = {"path": str(target), "sha256": hashlib.sha256(data).hexdigest(),
                                  "mode": mode()}
                return read_ppm(target)[2]

            def press(key, hold=0.2):
                qmp.key(key, hold)
                time.sleep(0.35)  # the release must also pass the ADC debounce

            def leave_camera(attempts=12):
                """Escape until the menu is active again.

                While the firmware is busy encoding or processing a photo it
                drops key events, so a single Escape is not enough.
                """
                for _ in range(attempts):
                    press("esc")
                    deadline = time.time() + 8
                    while time.time() < deadline:
                        if mode() == 6:
                            return
                        time.sleep(0.25)
                raise TimeoutError("native back returns to menu")

            wait_for(proc, lambda: mode() == 6 and picture_ready(), "automatic stock menu")
            time.sleep(1)
            initial = save("01-menu")
            press("right")
            assert mode() == 6, "navigation must not enter an application"
            moved = save("02-selection")
            assert moved != initial, "native navigation must change the menu"
            press("left")
            press("ret")
            wait_for(proc, lambda: mode() == 3 and picture_ready(), "native OK opens camera")
            time.sleep(1)
            first = save("03-camera")
            time.sleep(0.7)
            second = save("04-camera-motion")
            assert first != second, "synthetic sensor marker must move"
            if selfie:
                start = value(OBSERVATIONS[profile.name]["camera"], 1)
                initial_gpio = struct.unpack_from("<I", (directory / "sfr.bin").read_bytes(),
                                                  0x8010)[0]
                press("tab")
                wait_for(proc, lambda: value(OBSERVATIONS[profile.name]["camera"], 1) != start,
                         "native second-camera switch", timeout=20)
                time.sleep(1.5)
                alternate = save("05-selfie")
                assert alternate != second, "second sensor must produce a different pattern"
                sfr = (directory / "sfr.bin").read_bytes()
                gpio = struct.unpack_from("<I", sfr, 0x8010)[0]
                assert (gpio ^ initial_gpio) & 0x800, "native PE11 must change"
                report["selfie"] = {"initial_sensor": start,
                                    "selected_sensor": value(OBSERVATIONS[profile.name]["camera"], 1),
                                    "gpio_e": gpio, "initial_gpio_e": initial_gpio}
                press("tab")
                wait_for(proc, lambda: value(OBSERVATIONS[profile.name]["camera"], 1) == start,
                         "native switch back", timeout=20)
                time.sleep(1.5)
                restored = save("05-front-restored")
                for x, y in ((80, 60), (120, 60), (160, 60), (80, 120), (120, 120), (160, 120)):
                    offset = (y * 320 + x) * 3
                    assert restored[offset:offset + 3] == second[offset:offset + 3]
                report["selfie"]["switch_back"] = "passed"
            if capture:
                log_start = (directory / "qemu.log").stat().st_size
                press("spc")
                wait_for(proc, lambda: any(line.startswith(b"jpga: ") and b" -> " in line
                         for line in (directory / "qemu.log").read_bytes()[log_start:].splitlines()),
                         "native JPEG encode", timeout=30)
                if profile.name == "z12":
                    # After the hardware encode the firmware decodes and scales
                    # the 12 MP frame in guest code. Under TCG that takes
                    # seconds, during which the camera drops key events.
                    wait_for(proc, lambda: value(0x020a78c0, 1) == 0 and
                             value(0x020a78c6, 1) == 1,
                             "native photo pipeline completes", timeout=180)
                    wait_for(proc, lambda: value(0x020a78ac, 1) != 2,
                             "camera window idle", timeout=180)
                else:
                    time.sleep(15)
                save("05-after-shutter")
            leave_camera()
            time.sleep(0.5)
            save("06-back")
        except Exception as error:
            report["failure"] = str(error)
            if qmp is not None:
                try:
                    registers = qmp.call("human-monitor-command", **{"command-line": "info registers"})
                    (directory / "failure-registers.txt").write_text(registers)
                    qmp.memory(0x02000000, 0x800000, directory / "failure-ram.bin")
                except Exception as diagnostic_error:
                    report["diagnostic_error"] = str(diagnostic_error)
            (directory / "failure.json").write_text(json.dumps(report, indent=2) + "\n")
            raise
        finally:
            if qmp is not None:
                qmp.close()
            if proc.poll() is None:
                proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    # The guest is stopped: never traverse FAT concurrently with guest writes.
    with FAT16(card) as sd:
        root = sd.entries()
        assert {"VIDEO", "PHOTO", "MP3", "VIDEO.LST"}.issubset(root)
        after = {name: hashlib.sha256(sd.read_file("PHOTO/" + name)).hexdigest()
                 for name in before}
        assert before == after, "fixture JPEG bytes changed"
        if capture:
            from PIL import Image
            import io
            from sd_read import jpeg_packets
            new = set(sd.entries("PHOTO")) - set(before)
            assert new, "no saved photo"
            verified = {}
            for name in sorted(new):
                payload = sd.read_file("PHOTO/" + name)
                packets = jpeg_packets(payload)
                dimensions = {}
                for label, data in packets.items():
                    with Image.open(io.BytesIO(data)) as im:
                        im.load()
                        dimensions[label] = list(im.size)
                verified[name] = {"sha256": hashlib.sha256(payload).hexdigest(),
                                  "dimensions": dimensions}
            report["capture"] = verified
    report["sd_mount"] = {"root_entries": sorted(root), "preserved_photos": after}
    if printer:
        pages = sorted(p.name for p in (directory / "prints").glob("*.png"))
        report["printer"] = {"directory": str(directory / "prints"), "pages": pages}
    (directory / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="new private evidence directory")
    parser.add_argument("--qemu", default=DEFAULT_QEMU)
    parser.add_argument("--selfie", action="store_true")
    parser.add_argument("--capture", action="store_true")
    parser.add_argument("--printer", action="store_true",
                        help="enable thermal-head capture and report emitted pages")
    args = parser.parse_args()
    result = exercise(args.image, args.output, args.qemu, args.selfie, args.capture,
                      args.printer)
    print(f"PASS {result['profile']}: native menu/navigation/camera/motion/back; SD preserved")
    if "printer" in result:
        print(f"  printer pages: {result['printer']['pages']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
