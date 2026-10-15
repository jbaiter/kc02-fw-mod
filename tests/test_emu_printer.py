"""Test the distributed GPIO backend, independently of guest application state."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def printer_pages(tmp_path_factory):
    pytest.importorskip("PIL.Image")
    if not shutil.which("cc") or not shutil.which("pkg-config"):
        pytest.skip("C compiler/pkg-config unavailable")
    flags = subprocess.run(["pkg-config", "--cflags", "--libs", "glib-2.0", "libpng"],
                           capture_output=True, text=True)
    if flags.returncode:
        pytest.skip("glib/libpng development files unavailable")
    directory = tmp_path_factory.mktemp("printer-unit")
    # The backend is ordinary repository source, so the test compiles it as is.
    shutil.copy(ROOT / "tools/emu/qemu/ax3292/ax3292-printer.h", directory / "printer.h")
    source = r'''
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "printer.h"
static uint32_t regs[0x10000 / 4];
static AxPrinter p;
static void emit(unsigned spr, uint32_t value) {
    uint32_t old = regs[spr >> 2]; regs[spr >> 2] = value;
    ax_printer_gpio(&p, spr, old, value, regs);
}
static void bit(unsigned spr, unsigned mask, bool value) {
    emit(spr, (regs[spr >> 2] & ~mask) | (value ? mask : 0));
}
static void power(bool on) { bit(0x8004, 0x40, on); }
static void load(unsigned dots, unsigned pattern) {
    for (unsigned x = 0; x < dots; x++) {
        bool value = pattern == 999 ? !(x % 2) : x < pattern;
        bit(0x800c, 0x8000, value);
        bit(0x800c, 0x2000, true); bit(0x800c, 0x2000, false);
    }
    bit(0x8004, 0x20, true); bit(0x8004, 0x20, false);
}
static void strobe(void) { bit(0x8000, 0x200, true); bit(0x8000, 0x200, false); }
static void phase(unsigned vector) {
    bit(0x800c, 1, vector & 1);
    bit(0x8000, 8, vector & 4);
    emit(0x8010, (regs[0x8010 >> 2] & ~0xc00) |
         ((vector & 2) ? 0x400 : 0) | ((vector & 8) ? 0x800 : 0));
}
static const unsigned default_vectors[8] = {10,8,9,1,5,4,6,2};
static const unsigned z12_vectors[8] = {6,14,10,11,9,13,5,7};
static void feed(const unsigned *vectors, unsigned steps) {
    phase(vectors[0]);
    for (unsigned i = 1; i <= steps; i++) { phase(vectors[i % 8]); }
}
int main(int argc, char **argv) {
    p.directory = argv[1]; p.paper = true;
    memcpy(p.phases, default_vectors, sizeof(default_vectors));
    power(true); load(384, 999); strobe(); strobe(); feed(default_vectors, 4); power(false);
    power(true);
    for (unsigned n = 100; n <= 400; n += 100) { load(384, n); strobe(); }
    power(false);
    power(true); load(383, 400); strobe(); load(383, 0); strobe(); power(false);
    p.paper = false; power(true); load(384, 400); strobe(); power(false);
    p.paper = true; power(true); load(384, 400); strobe();
    feed(default_vectors, 4 * (AX_PRINT_ROWS + 1)); power(false);
    /* A different board may wire the same four platen lines to other SFR bits,
       which reorders the phase vector. */
    memcpy(p.phases, z12_vectors, sizeof(z12_vectors));
    power(true); load(384, 999); strobe(); strobe(); feed(z12_vectors, 4); power(false);
    g_free(p.hits);
    return 0;
}
'''
    (directory / "test.c").write_text(source)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-parameter",
                    str(directory / "test.c"), "-o", str(directory / "test"),
                    *shlex.split(flags.stdout)], check=True, capture_output=True)
    (directory / "print-00001.png").write_bytes(b"existing image")
    (directory / "print-00002.png.part").write_bytes(b"other job reservation")
    result = subprocess.run([str(directory / "test"), str(directory)], check=True,
                            capture_output=True, text=True)
    return directory, result.stderr


def test_pulse_raster_and_motor_feed(printer_pages):
    from PIL import Image
    directory, log = printer_pages
    with Image.open(directory / "print-00003.png") as image:
        assert image.mode == "L" and image.size == (384, 2)
        assert image.crop((0, 0, 384, 1)).tobytes() == bytes([0, 255]) * 192
        assert image.crop((0, 1, 384, 2)).getextrema() == (255, 255)
    assert "steps=4 bad_steps=0" in log


def test_grayscale_comes_from_strobes_not_host_dither(printer_pages):
    from PIL import Image
    directory, _ = printer_pages
    with Image.open(directory / "print-00004.png") as image:
        assert image.size == (384, 1)
        assert image.tobytes() == bytes([0]) * 100 + bytes([64]) * 100 + bytes([127]) * 100 + bytes([191]) * 84


def test_383_clock_driver_retains_real_shift_register_bit(printer_pages):
    from PIL import Image
    directory, _ = printer_pages
    with Image.open(directory / "print-00005.png") as image:
        assert image.getextrema() == (127, 127)


def test_board_specific_platen_phase_order(printer_pages):
    """The Z-12 wires the platen lines to different bits, reordering phases."""
    from PIL import Image
    directory, log = printer_pages
    with Image.open(directory / "print-00006.png") as image:
        assert image.mode == "L" and image.size == (384, 2)
    # Four forward steps were taken under the second phase order.
    assert log.count("steps=4 bad_steps=0") == 2


def test_exclusive_atomic_publication_and_bounds(printer_pages):
    directory, log = printer_pages
    assert (directory / "print-00001.png").read_bytes() == b"existing image"
    assert (directory / "print-00002.png.part").read_bytes() == b"other job reservation"
    assert sorted(p.name for p in directory.glob("print-*.png")) == [
        "print-00001.png", "print-00003.png", "print-00004.png", "print-00005.png",
        "print-00006.png"]
    assert len(list(directory.glob("*.part"))) == 1
    assert "discarded page exceeding bounded raster" in log


def test_launcher_printer_options(tmp_path):
    import json
    image = tmp_path / "firmware.bin"
    image.write_bytes(b"\0" * 0x3000)
    qemu = tmp_path / "qemu"
    output = tmp_path / "argv.json"
    qemu.write_text("#!/usr/bin/env python3\nimport json, sys\n"
                    f"open({str(output)!r}, 'w').write(json.dumps(sys.argv[1:]))\n")
    qemu.chmod(0o755)
    command = ["python3", str(ROOT / "tools/emu/run.py"), str(image),
               "--qemu", str(qemu), "--interactive", "--printer-output", str(tmp_path / "prints")]
    env = dict(os.environ, AX3292_PRINTER_NO_PAPER="1")
    subprocess.run(command, env=env, check=True, capture_output=True)
    argv = json.loads(output.read_text())
    # Board wiring is a machine property, not an environment variable.
    machine = argv[argv.index("-M") + 1]
    assert f"printer-dir={tmp_path / 'prints'}" in machine
    assert "printer-no-paper=off" in machine
    assert not [a for a in argv if a.startswith("AX3292_PRINTER")]
    subprocess.run(command + ["--printer-no-paper"], check=True, capture_output=True)
    assert "printer-no-paper=on" in json.loads(output.read_text())[
        json.loads(output.read_text()).index("-M") + 1]
    result = subprocess.run(command[:-2] + ["--printer-no-paper"], capture_output=True, text=True)
    assert result.returncode == 2
    assert "requires --printer-output" in result.stderr
