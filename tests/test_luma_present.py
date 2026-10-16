"""Exercise the production presenter with a delayed DE frame-boundary latch.

These are buffer-ownership checks, not evidence of tear-free physical scanout.
"""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


def test_present_never_reuses_active_or_pending_buffer(tmp_path):
    cc = shutil.which(os.environ.get('CC', 'cc'))
    if not cc:
        pytest.skip('C compiler unavailable')
    board = (ROOT / 'custom-firmware/boards/ax3292.c').read_text()
    start = board.index('static void present(const u16 *f)')
    end = board.index('\n/* Raw keypad', start)
    source = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define BOARD_ID 0
#define LW 320
#define LCD_PITCH 256
#define Y_BYTES (320*LCD_PITCH)
#define UV_BYTES (160*LCD_PITCH)
static u8 lcd_y[2][Y_BYTES], lcd_uv[2][UV_BYTES];
static unsigned lcd_submitted;
static int lcd_diagnostics;
static u32 lcd_key_level, luma_frames, luma_de_timeouts;
static u16 frame[320*240];
static u32 regs[0x10000/4];
#define REG(n) regs[(n)/4]
static unsigned complete_after, service_calls;
static void lcd_service(void)
{
    service_calls++;
    if (complete_after && --complete_after == 0) {
        REG(0xc87c) = REG(0xc828) & 0xffffffu;
        REG(0xc800) &= ~2u;
    }
}
static u8 clip(int v) { return v<0 ? 0 : v>255 ? 255 : v; }
static u32 address(const void *p) { return (u32)(uintptr_t)p; }
static unsigned checksum(const void *p, unsigned n)
{
    const u8 *b = p;
    unsigned h = 0;
    while (n--) h = h * 33u + *b++;
    return h;
}
''' + board[start:end] + r'''
int main(void)
{
    memset(lcd_y[0], 17, Y_BYTES);
    memset(lcd_uv[0], 128, UV_BYTES);
    memset(lcd_y[1], 33, Y_BYTES);
    memset(lcd_uv[1], 140, UV_BYTES);
    memset(frame, 0xff, sizeof(frame));
    REG(0xc800) = 0x81;  /* preserve unrelated configuration */
    REG(0xc87c) = address(lcd_y[0]) & 0xffffff;
    unsigned y0 = checksum(lcd_y[0], Y_BYTES);
    unsigned uv0 = checksum(lcd_uv[0], UV_BYTES);
    present(frame);
    assert(lcd_submitted == 1 && luma_de_timeouts == 0);
    assert(checksum(lcd_y[0], Y_BYTES) == y0);
    assert(checksum(lcd_uv[0], UV_BYTES) == uv0);
    assert(lcd_y[1][0] == 255 && lcd_uv[1][0] == 128);
    assert(lcd_y[1][240] == 33);  /* padding stays intact */
    assert(REG(0xc828) == address(lcd_y[1]));
    assert(REG(0xc830) == address(lcd_uv[1]));
    assert(REG(0xc800) == 0x83);  /* swap is requested, not yet latched */

    unsigned y1 = checksum(lcd_y[1], Y_BYTES);
    unsigned uv1 = checksum(lcd_uv[1], UV_BYTES);
    memset(frame, 0, sizeof(frame));
    complete_after = 3;  /* previous submission must latch before reuse */
    service_calls = 0;
    present(frame);
    assert(service_calls == 4);  /* three wait polls plus new frame kick */
    assert(lcd_submitted == 0 && luma_de_timeouts == 0);
    assert(checksum(lcd_y[1], Y_BYTES) == y1);
    assert(checksum(lcd_uv[1], UV_BYTES) == uv1);
    assert(lcd_y[0][0] == 0 && lcd_uv[0][0] == 128);
    assert(REG(0xc828) == address(lcd_y[0]));

    /* A pending commit that never completes cannot permit either buffer write. */
    unsigned all_y = checksum(lcd_y, sizeof(lcd_y));
    unsigned all_uv = checksum(lcd_uv, sizeof(lcd_uv));
    present(frame);
    assert(luma_de_timeouts == 1 && lcd_submitted == 0);
    assert(checksum(lcd_y, sizeof(lcd_y)) == all_y);
    assert(checksum(lcd_uv, sizeof(lcd_uv)) == all_uv);

    /* A clear request with the WRONG active address is also not permission. */
    REG(0xc800) &= ~2u;
    present(frame);
    assert(luma_de_timeouts == 2 && lcd_submitted == 0);
    assert(checksum(lcd_y, sizeof(lcd_y)) == all_y);

    REG(0xc87c) = address(lcd_y[0]) & 0xffffff;
    lcd_diagnostics = 1;
    luma_frames = 100;
    lcd_key_level = 1023;
    y0 = checksum(lcd_y[0], Y_BYTES);
    present(frame);
    assert(lcd_submitted == 1);
    assert(checksum(lcd_y[0], Y_BYTES) == y0);
    assert(lcd_y[1][5*LCD_PITCH + 101] == 255);
    assert(lcd_y[1][5*LCD_PITCH + 100] == 0);
    assert(lcd_y[1][8*LCD_PITCH + 239] == 200);
    /* Hardware captured pixel bytes in this scalar before CSI bounds were fixed.
     * Reject that state BEFORE forming an out-of-range array pointer. */
    lcd_submitted = 0x5d5d5e5d;
    service_calls = 0;
    all_y = checksum(lcd_y, sizeof(lcd_y));
    present(frame);
    assert(luma_de_timeouts == 3 && service_calls == 0);
    assert(checksum(lcd_y, sizeof(lcd_y)) == all_y);
    return 0;
}
'''
    file = tmp_path / 'present.c'
    file.write_text(source)
    binary = tmp_path / 'present'
    subprocess.run([cc, '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-Wno-pointer-to-int-cast', '-fsanitize=undefined',
                    str(file), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
