#!/usr/bin/env python3
"""Exercise original KC02 UI + fake sensor through QEMU's real keypad input.

No firmware or application-state writes. Saves PPM screenshots and a JSON report.
Requires the patched qemu-system-or1k, but only Python's standard library.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time

from run import DEFAULT_QEMU

ORIGINAL_SHA256 = "218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5"


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    magic, dimensions, maximum, pixels = path.read_bytes().split(b"\n", 3)
    if magic != b"P6" or maximum != b"255":
        raise ValueError("expected an RGB P6/255 frame")
    w, h = map(int, dimensions.split())
    if w <= 0 or h <= 0 or len(pixels) != w * h * 3:
        raise ValueError("incomplete framebuffer")
    return w, h, pixels


class QMP:
    def __init__(self, path: Path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(5)
        self.socket.connect(str(path))
        self.stream = self.socket.makefile("rwb")
        greeting = json.loads(self.stream.readline())
        if "QMP" not in greeting:
            raise RuntimeError(f"bad QMP greeting: {greeting}")
        self.call("qmp_capabilities")

    def call(self, command: str, **arguments):
        self.stream.write(json.dumps({"execute": command, "arguments": arguments}).encode() + b"\n")
        self.stream.flush()
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError("QMP disconnected")
            result = json.loads(line)
            if "error" in result:
                raise RuntimeError(f"QMP {command}: {result['error']}")
            if "return" in result:
                return result["return"]

    def key(self, code: str):
        for down in (True, False):
            self.call("input-send-event", events=[{
                "type": "key", "data": {"down": down, "key": {"type": "qcode", "data": code}}
            }])
            if down:
                # ADC keypad debounce needs several samples; >500 ms is a
                # firmware long press, not the short navigation action.
                time.sleep(0.2)

    def memory(self, address: int, size: int, path: Path) -> bytes:
        self.call("pmemsave", val=address, size=size, filename=str(path))
        return path.read_bytes()

    def close(self):
        self.stream.close()
        self.socket.close()


def wait_for(proc, predicate, description: str, timeout: float = 20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"QEMU exited {proc.returncode} while waiting for {description}")
        if predicate():
            return
        time.sleep(0.05)
    raise TimeoutError(description)


def exercise(qemu: str, image: Path, directory: Path) -> dict:
    directory.mkdir(parents=True, exist_ok=True)
    frame = directory / "screen.ppm"
    app = directory / "app.bin"
    app.write_bytes(image.read_bytes()[0x2600:0xd3200])
    sock = directory / "qmp.sock"
    sock.unlink(missing_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("AX3292_")}
    env.update(AX3292_FLASH=str(image.resolve()), AX3292_SENSOR="1",
               AX3292_SPR_TRACE="0", AX3292_FB=str(frame), AX3292_FB_EVERY="5")
    command = [qemu, "-M", "ax3292", "-m", "64M", "-nodefaults",
               "-accel", "tcg,thread=multi", "-display", "none",
               "-monitor", "none", "-serial", "none", "-kernel", str(app),
               "-qmp", f"unix:{sock},server=on,wait=off"]
    qmp = None
    captures = {}
    with (directory / "qemu.log").open("wb") as log:
        proc = subprocess.Popen(command, env=env, stdout=log, stderr=log)
        try:
            wait_for(proc, sock.exists, "QMP socket")
            qmp = QMP(sock)
            scratch = directory / "memory.bin"

            def value(address, length=4):
                return int.from_bytes(qmp.memory(address, length, scratch), "little")

            def picture_ready():
                try:
                    w, h, pixels = read_ppm(frame)
                    return (w, h) == (320, 240) and len(set(pixels)) > 32
                except (OSError, ValueError):
                    return False

            def save(name):
                wait_for(proc, picture_ready, name)
                data = frame.read_bytes()
                target = directory / f"{name}.ppm"
                target.write_bytes(data)
                captures[name] = {"path": str(target), "sha256": hashlib.sha256(data).hexdigest()}
                return read_ppm(target)[2]

            # Read-only application observations, never boot-state seeding.
            wait_for(proc, lambda: value(0x020ccd54) == 6 and picture_ready(), "automatic menu")
            wait_for(proc, lambda: value(0x020ccec4) != 0, "six decoded menu icons")
            assert value(0x020d41d3, 1) == 0
            save("01-menu")
            qmp.key("right")
            wait_for(proc, lambda: value(0x020d41d3, 1) == 1, "RIGHT selects Video")
            time.sleep(0.3)
            save("02-right")
            qmp.key("left")
            wait_for(proc, lambda: value(0x020d41d3, 1) == 0, "LEFT selects Kamera")
            qmp.key("ret")

            def camera_ready():
                if not picture_ready():
                    return False
                pixels = read_ppm(frame)[2]
                r, g, b = pixels[(100 * 320 + 150) * 3:][:3]
                return g > 180 and r < 60 and b < 60

            wait_for(proc, camera_ready, "OK opens CSI colour-bar viewfinder")
            first = save("03-camera")
            time.sleep(0.7)
            second = save("04-camera-motion")
            assert first != second, "fake sensor marker must move"
            qmp.key("esc")
            wait_for(proc, lambda: value(0x020ccd54) == 6 and not camera_ready(), "ESC returns to menu")
            save("05-back")
        finally:
            if qmp is not None:
                qmp.close()
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
    errors = (directory / "qemu.log").read_text(errors="replace")
    assert "assertion failed" not in errors and "ERROR:" not in errors
    result = {"passed": True, "firmware_sha256": ORIGINAL_SHA256,
              "command": command, "screenshots": captures}
    (directory / "report.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--qemu", default=DEFAULT_QEMU)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=1)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    if hashlib.sha256(args.image.read_bytes()).hexdigest() != ORIGINAL_SHA256:
        parser.error("this smoke test is specific to firmware/original.bin")
    for i in range(args.runs):
        output = args.output.resolve() / f"run-{i + 1}"
        exercise(args.qemu, args.image, output)
        print(f"PASS {i + 1}: menu -> RIGHT/LEFT -> camera/motion -> menu ({output})", flush=True)


if __name__ == "__main__":
    main()
