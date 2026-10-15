"""Drive Luma's SCCB layer (custom-firmware/include/ax_sensor.h) against the
production emulator I2C model. No camera is accessed.

This pins the two things that matter: a powered H63P answers 0x48 at register
0x0b, and an unpowered or absent imager is reported as a definite failure rather
than a bogus 0xff reading (which is what the bench saw on real hardware while the
sensor was not powered).
"""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
EMU = ROOT / 'tools/emu/qemu/ax3292'
LUMA_INC = ROOT / 'custom-firmware/include'
FIXTURE = json.loads((ROOT / 'tests/fixtures/hardware/original-2026-10-15.json').read_text())

SOURCE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define AX3292_RAM_SIZE (64u*1024*1024)
typedef struct { unsigned unused; } QemuConsole;
typedef struct CPU { struct { uint32_t picsr; } env; } CPUState;
typedef struct {
    bool sensor, sensor_3a, dual_sensor, panel_ccw;
    uint32_t sfr[0x10000/4], csi_lcd_size, key_value, tick_count;
    unsigned i2c_phase[2], key_adc[11];
    bool i2c_selected[2]; uint8_t i2c_offset[2], sensor_regs[2][256];
    uint8_t *ram_ptr, *screen_pixels; CPUState *cpu;
} Ax3292State;
#define AX3292_MACHINE(p) ((Ax3292State *)(p))
#include "ax3292-sensor.h"

static Ax3292State S;

static uint32_t test_read(uint32_t n);
static void test_write(uint32_t n, uint32_t v);
#define AX_SENSOR_READ(n)    test_read(n)
#define AX_SENSOR_WRITE(n, v) test_write(n, v)
#include "ax_sensor.h"

static uint32_t test_read(uint32_t n)
{
    /* The command register is write-only in hardware and in the model. */
    if (n == AX_IIC0_BASE + AX_IIC_OFF_CMD) { return 0; }
    return S.sfr[n >> 2];
}

static void test_write(uint32_t n, uint32_t v)
{
    if (n == AX_IIC0_BASE + AX_IIC_OFF_CMD) { ax3292_i2c_command(&S, 0, v); return; }
    S.sfr[n >> 2] = v;
}

static void reset_state(bool present, bool is_3a)
{
    memset(&S, 0, sizeof(S));
    S.sensor = present;
    S.sensor_3a = is_3a;
    S.cpu = NULL;
}

int main(void)
{
    ax_u8 id = 0, v = 0;

    /* 1. Powered H63P: the stock resource table expects 0x48 at register 0x0b. */
    reset_state(true, false);
    assert(ax_sensor_detect_h63p(&id) == 1);
    assert(id == 0x48);

    /* 2. A write is observable on read-back, so the write path is real. */
    reset_state(true, false);
    assert(ax_sensor_write_reg(AX_SENSOR_SLAVE_ADDR, 0x20, 0x55) == 1);
    assert(ax_sensor_read_reg(AX_SENSOR_SLAVE_ADDR, 0x20, &v) == 1);
    assert(v == 0x55);

    /* 3. Unpowered/absent imager NACKs. This must be a hard failure, not 0xff,
     *    which is the exact ambiguity the bench hit on real hardware. */
    reset_state(false, false);
    id = 0xAA;
    assert(ax_sensor_read_reg(AX_SENSOR_SLAVE_ADDR, AX_SENSOR_ID_REG, &id) == 0);
    assert(ax_sensor_detect_h63p(&id) == 0);

    /* 4. The 3A variant answers on its own slave address and id register. */
    reset_state(true, true);
    assert(ax_sensor_read_reg(0xdc, 0xfc, &v) == 1);
    assert(v == 0x3a);

    return 0;
}
'''


def test_luma_sccb_matches_emulator_model(tmp_path):
    cc = shutil.which(os.environ.get('CC', 'cc'))
    if not cc:
        pytest.skip('C compiler unavailable')
    file = tmp_path / 'test.c'
    file.write_text(SOURCE)
    binary = tmp_path / 'test'
    subprocess.run([cc, '-std=gnu11', '-Wall', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-const-variable', '-Wno-unused-but-set-variable',
                    '-fsanitize=undefined',
                    '-I', str(EMU), '-I', str(LUMA_INC),
                    str(file), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


POWER_SOURCE = r'''
#include <assert.h>
#include <stdint.h>
static uint32_t regs[0x10000/4];
static uint32_t test_read(uint32_t n) { return regs[n >> 2]; }
static void test_write(uint32_t n, uint32_t v) { regs[n >> 2] = v; }
#define AX_SENSOR_READ(n)     test_read(n)
#define AX_SENSOR_WRITE(n, v) test_write(n, v)
#include "ax_cam_power.h"

int main(void)
{
    uint32_t v;

    /* LSEN carries the DVDD selector in bits 11:7 and an enable in bit 12. */
    test_write(AX_CAM_SYS_LDO, 0);
    ax_cam_ldo_set(AX_LDO_LSEN, AX_H63P_DVDD, 1);
    v = test_read(AX_CAM_SYS_LDO);
    assert(v & 0x1000u);
    assert(((v >> 7) & 0x1fu) == 6u);
    assert((v & 0x20u) == 0u);

    /* HSEN carries the AVDD selector in bits 4:0 and an enable in bit 5. */
    test_write(AX_CAM_SYS_LDO, 0);
    ax_cam_ldo_set(AX_LDO_HSEN, AX_H63P_AVDD, 1);
    v = test_read(AX_CAM_SYS_LDO);
    assert(v & 0x20u);
    assert((v & 0x1fu) == 16u);

    /* Disabling one rail must not disturb the other. */
    ax_cam_ldo_set(AX_LDO_LSEN, AX_H63P_DVDD, 1);
    ax_cam_ldo_set(AX_LDO_HSEN, AX_H63P_AVDD, 0);
    v = test_read(AX_CAM_SYS_LDO);
    assert((v & 0x20u) == 0u);
    assert(((v >> 7) & 0x1fu) == 6u);

    /* Regression for the bench mistake: index 6 does not exist and must be a
     * no-op. sensorprobe called ldo(6,1,0) and so never powered the imager. */
    test_write(AX_CAM_SYS_LDO, 0x12345678u);
    ax_cam_ldo_set(6u, 1u, 1);
    assert(test_read(AX_CAM_SYS_LDO) == 0x12345678u);

    /* MCLK: 288 MHz / freq - 1 in bits 12:8, plus the PE10 output mux. */
    test_write(AX_CAM_MCLK_CTL, 0);
    test_write(AX_CAM_MCLK_DIV, 0xffffffffu);
    test_write(AX_CAM_MCLK_EN, 0xffffffffu);
    test_write(AX_CAM_GPIO_MAP_PE, 0);
    test_write(AX_CAM_GPIO_DIR_PE, 0xffffffffu);
    test_write(AX_CAM_PMAPCON2, 0xffffffffu);
    ax_cam_mclk_set(AX_H63P_MCLK_HZ);
    assert(test_read(AX_CAM_MCLK_CTL) & 0x04000000u);
    assert(((test_read(AX_CAM_MCLK_DIV) >> 8) & 0x1fu) == 11u);   /* 24 MHz */
    assert(test_read(AX_CAM_GPIO_MAP_PE) & AX_CAM_PE10);
    assert((test_read(AX_CAM_GPIO_DIR_PE) & AX_CAM_PE10) == 0u);
    assert(((test_read(AX_CAM_PMAPCON2) >> 10) & 3u) == 2u);
    assert((test_read(AX_CAM_MCLK_EN) & 0x20u) == 0u);

    ax_cam_mclk_set(AX_H63P_ID_MCLK_HZ);
    assert(((test_read(AX_CAM_MCLK_DIV) >> 8) & 0x1fu) == 17u);   /* 16 MHz */

    /* An out-of-range request must stay inside bits 12:8 and must not corrupt
     * the fields around it. The stock expression shifts without masking, so this
     * is a deliberate hardening; valid clocks (9..288 MHz) are unaffected. */
    test_write(AX_CAM_MCLK_DIV, 0xfffe0000u);
    ax_cam_mclk_set(1u);
    assert((test_read(AX_CAM_MCLK_DIV) & 0xfffe0000u) == 0xfffe0000u);
    assert((test_read(AX_CAM_MCLK_DIV) & 0x00001f00u) != 0u);
    return 0;
}
'''


def test_luma_camera_power_registers(tmp_path):
    cc = shutil.which(os.environ.get('CC', 'cc'))
    if not cc:
        pytest.skip('C compiler unavailable')
    file = tmp_path / 'power.c'
    file.write_text(POWER_SOURCE)
    binary = tmp_path / 'power'
    subprocess.run([cc, '-std=gnu11', '-Wall', '-Werror', '-Wno-unused-function',
                    '-fsanitize=undefined',
                    '-I', str(LUMA_INC), str(file), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def test_fixture_records_the_power_dependency():
    """The 0xff bench reading is explained by sensor power, not by the protocol."""
    sensor = FIXTURE['sensor']
    assert sensor['name'] == 'H63P'
    assert int(sensor['id_register'], 16) == 0x0b
    assert int(sensor['expected_id'], 16) == 0x48
    assert int(sensor['ops'], 16) == 0x020e32a0
    # Stock autodetect did select the real H63P; only the standalone helper read
    # returned 0xff, which is why the ID path must report NACK rather than 0xff.
    assert int(sensor['independent_read'], 16) == 0xff
