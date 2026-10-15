"""Build profiles and the distributed dual-camera peripheral backend."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/emu"))
from profiles import PROFILES, detect


@pytest.mark.parametrize("name,file", [
    ("original", "original.bin"),
    ("x8d", "mystiluck-x8d-8e0636e1.bin"),
    ("z12", "hmollimt-z12-10da5dbf.bin"),
])
def test_dump_profiles(name, file):
    image = ROOT / "firmware" / file
    if not image.exists():
        pytest.skip("private firmware dump unavailable")
    profile = detect(image)
    assert profile.name == name
    assert profile == PROFILES[name]
    assert 0 < profile.app_offset < profile.app_end <= image.stat().st_size
    env = profile.options()
    assert int(env["entry"], 0) == profile.entry
    assert len(env["key-values"].split(":")) == 6
    assert len(env["extra-keys"].split(":")) == 5
    assert env["sensor-3a"] == ("on" if name != "original" else "off")
    assert env["dual-sensor"] == ("on" if name == "z12" else "off")
    assert not [v for v in env.values() if "," in v], "a comma would end the -M option"
    assert profile.key_channel == 3


def test_unknown_hash_falls_back(tmp_path):
    image = tmp_path / "unknown.bin"
    image.write_bytes(b"unknown")
    assert detect(image).name == "original"


def test_z12_native_wiring():
    profile = PROFILES["z12"]
    assert profile.key_adc == (620, 775, 328, 238, 512, 415)
    assert profile.extra_keys[3] == profile.key_adc[3]  # Tab aliases native Right
    assert profile.flash_id == "c84016"
    assert profile.flash_uid == "4d37393830109b3f"
    # The Z-12 wires the platen-motor lines to different SFR bits.
    assert profile.printer_phases == (6, 14, 10, 11, 9, 13, 5, 7)
    assert PROFILES["original"].printer_phases == (10, 8, 9, 1, 5, 4, 6, 2)
    assert PROFILES["x8d"].printer_phases == PROFILES["original"].printer_phases
    for name, profile in PROFILES.items():
        phases = profile.options()["printer-phases"].split(":")
        assert len(phases) == 8 and all(0 <= int(v) <= 15 for v in phases), name


def test_sensor_backend(tmp_path):
    if not shutil.which("cc"):
        pytest.skip("C compiler unavailable")
    directory = tmp_path
    # The backend is ordinary repository source, so the test compiles it as is.
    shutil.copy(ROOT / "tools/emu/qemu/ax3292/ax3292-sensor.h", directory / "sensor.h")
    (directory / "test.c").write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define AX3292_RAM_SIZE 16384
struct CPU { struct { uint32_t picsr; } env; };
typedef struct {
    bool sensor, sensor_3a, dual_sensor;
    uint32_t sfr[0x10000/4];
    unsigned i2c_phase[2]; bool i2c_selected[2]; uint8_t i2c_offset[2];
    uint8_t sensor_regs[2][256]; uint8_t *ram_ptr;
    unsigned tick_count; struct CPU *cpu; uint32_t csi_lcd_size;
} Ax3292State;
#include "sensor.h"
static void tx(Ax3292State *s, unsigned value, unsigned command) {
    s->sfr[0x909c/4] = value; ax3292_i2c_command(s, 0, command);
}
static void reg(Ax3292State *s, unsigned value) {
    tx(s, 0xdc, 0x90); tx(s, 0x1e, 0x10); tx(s, value, 0x10);
}
int main(void) {
    uint8_t ram[AX3292_RAM_SIZE] = {0}, front[3072];
    struct CPU cpu = {0};
    Ax3292State s = {.sensor=true, .sensor_3a=true, .dual_sensor=true,
                      .ram_ptr=ram, .cpu=&cpu};
    s.sfr[0x8010/4] = 0x800;
    assert(ax3292_sensor_index(&s) == 0);
    reg(&s, 0); assert(s.sensor_regs[0][0x1e] == 0);
    assert(ax3292_sensor_fill(&s, 128, 4096, 64, 48, 64));
    memcpy(front, ram+128, sizeof(front));
    s.sfr[0x8010/4] = 0;
    assert(ax3292_sensor_index(&s) == 1);
    assert(ax3292_sensor_fill(&s, 128, 4096, 64, 48, 64));
    assert(memcmp(front, ram+128, sizeof(front)) != 0);
    reg(&s, 0x30); assert(s.sensor_regs[1][0x1e] == 0x30);
    assert(s.sensor_regs[0][0x1e] == 0);
    tx(&s, 0xdc, 0x90); tx(&s, 0xfc, 0x10); tx(&s, 0xdd, 0x90);
    ax3292_i2c_command(&s, 0, 0x20); assert(s.sfr[0x9098/4] == 0x3a);
    s.sfr[0x8010/4] = 0x800;
    assert(ax3292_sensor_fill(&s, 128, 4096, 64, 48, 64));
    assert(memcmp(front, ram+128, sizeof(front)) == 0);
    reg(&s, 0x20); assert(ax3292_sensor_fill(&s, 128, 4096, 64, 48, 64));
    for (unsigned x=0; x<64; x++) assert(ram[128+x] == front[63-x]);
    reg(&s, 0x30); assert(ax3292_sensor_fill(&s, 128, 4096, 64, 48, 64));
    for (unsigned y=0; y<48; y++) for (unsigned x=0; x<64; x++)
        assert(ram[128+y*64+x] == front[(47-y)*64+63-x]);
    tx(&s, 0xb0, 0x90); tx(&s, 0xfc, 0x10); tx(&s, 0xb1, 0x90);
    ax3292_i2c_command(&s, 0, 0x20);
    assert(s.sfr[0x9098/4] == 0xff && (s.sfr[0x90a4/4] & 0x80));
    s.dual_sensor=false; s.sfr[0x8010/4]=0;
    assert(ax3292_sensor_index(&s) == 0);
    assert(ax3292_sensor_fill(&s, 128, 4096, 64, 48, 32));
    assert(ax3292_sensor_fill(&s, 14816, 4096, 64, 48, 32));
    assert(!ax3292_sensor_fill(&s, 14817, 4096, 64, 48, 32));
    assert(!ax3292_sensor_fill(&s, 16000, 4096, 64, 48, 64));
    s.sfr[0xb008/4] = 1u<<19; ax3292_sensor_frame(&s);
    assert(!s.sfr[0xb008/4] && !s.sfr[0xb00c/4]);
    s.sfr[0xb000/4] = 1; s.sfr[0xb014/4] = 0x80000008;
    ax3292_sensor_frame(&s);
    assert(s.sfr[0xb00c/4] == 8 && cpu.env.picsr == (1u<<14));
    s.sfr[0xb060/4]=128; s.sfr[0xb064/4]=4096;
    s.sfr[0xb068/4]=64; s.sfr[0xb180/4]=(48u<<16)|64;
    s.sfr[0xb008/4]=1u<<19;  /* the LCD path uses the size latched by LS_KS */
    s.sfr[0xb018/4]=0x240000; ax3292_sensor_frame(&s);
    assert(s.sfr[0xb018/4] == 0x200000);
    assert(s.sfr[0xb00c/4] == 0xa);
    return 0;
}
''')
    binary = tmp_path / "test"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Werror",
                    str(tmp_path / "test.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
