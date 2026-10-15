"""Measured original-KC02 contracts, exercised against production backends.

Snapshots are evidence, not emulator reset values or injected application state.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tests/fixtures/hardware/original-2026-10-15.json'
MEASURED = json.loads(FIXTURE.read_text())


def test_measured_key_ladder_matches_profile():
    sys.path.insert(0, str(ROOT / 'tools/emu'))
    from profiles import PROFILES
    profile = PROFILES['original']
    by_key = dict(zip(MEASURED['key_adc']['order'], MEASURED['key_adc']['centres']))
    assert profile.key_adc == tuple(by_key[k] for k in ('up', 'down', 'left', 'right', 'ok', 'escape'))
    assert profile.key_channel == MEASURED['adc_conversion']['channel']
    assert profile.flash_id == MEASURED['flash']['jedec_id']
    assert profile.options()['flash-id'] == MEASURED['flash']['jedec_id']
    assert PROFILES['x8d'].flash_id == 'ef4016'  # not inferred from the original


def test_measured_adc_and_timer_backend(tmp_path):
    cc = shutil.which(os.environ.get('CC', 'cc'))
    if not cc:
        pytest.skip('C compiler unavailable')
    adc = MEASURED['adc_conversion']
    timer = MEASURED['timer_detail']
    source = r'''
#include <assert.h>
#include <stdint.h>
#include "ax3292-adc.h"
#include "ax3292-timer.h"
#include "ax_time.h"
int main(void) {
    uint32_t complete = ax3292_adc_completed_control(BEFORE | 0x80 | 0x40);
    assert(complete == AFTER);
    assert(ax3292_adc_sample(CHANNEL, CHANNEL, SAMPLE) == SAMPLE);
    assert(ax3292_adc_sample(1, CHANNEL, SAMPLE) == 700);
    assert(ax3292_adc_sample(11, CHANNEL, SAMPLE) == 280);
    Ax3292TickTimer t = {.preload = UINT32_MAX};
    assert(ax3292_tt_read(&t, 0x9900, 1000000000) == 0);
    assert(ax3292_tt_read(&t, 0x9908, 2000000000) == 0);
    assert(ax3292_tt_read(&t, 0x9904, 2000000000) == UINT32_MAX);
    ax3292_tt_write(&t, 0x9900, START, 2000000000);
    assert(ax3292_tt_read(&t, 0x9900, 2000000000) == START_CONTROL);
    uint32_t count = ax3292_tt_read(&t, 0x9908, 2050000000);
    /* Rate is approximate: compare within 2% of the 50ms hardware sample. */
    assert(count > MEASURED_COUNT * 98ULL / 100);
    assert(count < MEASURED_COUNT * 102ULL / 100);
    ax3292_tt_write(&t, 0x9900, 0, 2050000000);
    assert(ax3292_tt_read(&t, 0x9908, 2100000000) == 0);
    ax3292_tt_write(&t, 0x9900, 2, 2100000000);
    assert(ax3292_tt_read(&t, 0x9908, 2100001000) == 128);
    ax3292_tt_write(&t, 0x9900, 0x40, 2100001000);
    assert(ax3292_tt_read(&t, 0x9900, 2200000000) == 0);
    assert(ax3292_tt_read(&t, 0x9908, 2200000000) == 0);
    ax3292_tt_write(&t, 0x9900, 0x42, 2200000000);
    ax3292_tt_write(&t, 0x9908, UINT32_MAX - 15, 2200000000);
    assert(ax3292_tt_read(&t, 0x9908, 2200001000) == 112);
    /* Sub-tick reads do not discard fractional time. */
    ax3292_tt_write(&t, 0x9908, 0, 2200001000);
    for (unsigned n = 1; n <= 1000; n++) {
        ax3292_tt_read(&t, 0x9908, 2200001000 + n);
    }
    assert(t.count == 128);
    /* Luma must extend raw 32-bit ticks, not divide before handling wrap. */
    AxTime clock = {0, 0, 0};
    assert(ax_time_ms(&clock, 127999) == 0);
    assert(ax_time_ms(&clock, 128000) == 1);
    for (uint64_t ms = 41; ms <= 80001; ms += 40) {
        assert(ax_time_ms(&clock, (uint32_t)(ms * 128000)) == ms);
    }
    return 0;
}
'''
    defines = {
        'BEFORE': adc['before_control'], 'AFTER': adc['after_control'],
        'CHANNEL': adc['channel'], 'SAMPLE': adc['sample'],
        'START': timer['start_write'], 'START_CONTROL': timer['start_control'],
        'MEASURED_COUNT': timer['after_delay_count'] - timer['start_count'],
    }
    file = tmp_path / 'test.c'
    file.write_text(source)
    binary = tmp_path / 'test'
    subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    '-I', str(ROOT / 'tools/emu/qemu/ax3292'),
                    '-I', str(ROOT / 'custom-firmware/include'),
                    *[f'-D{k}={v}' for k, v in defines.items()], str(file), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def test_measurement_state_is_not_a_reset_template():
    registers = {int(k, 0): int(v, 0) for k, v in MEASURED['passive_spr'].items()}
    assert (registers[0xc810] & 0xffff) + 1 == 240
    assert (registers[0xc810] >> 16) + 1 == 320
    assert registers[0xc82c] * 4 == registers[0xc834] * 4 == 256
    # CSI is idle in this USB-mode snapshot, not absent silicon.
    assert registers[0xb060] == registers[0xb064] == registers[0xb068] == 0
    assert MEASURED['sensor']['name'] == 'H63P'
    assert timer_restored(MEASURED['timer_detail'])


def timer_restored(measured):
    return measured['original'] == measured['restored']
