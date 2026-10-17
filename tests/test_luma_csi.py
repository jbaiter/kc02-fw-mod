"""Execute Luma's actual polling service against fake CSI registers, without USB.

The stock normal-frame callback (0x0203a2f0) restores both destination addresses
and calls csiLCDDmaKick. PIP reset belongs to sensor-error recovery, not this path.
These tests check software sequencing, not silicon timing or panel quality.
"""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize('bench', [False, True])
def test_dma_guards_and_bench_spill_capacity(tmp_path, bench):
    cc = shutil.which(os.environ.get('CC', 'cc'))
    if not cc:
        pytest.skip('C compiler unavailable')
    source = r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "ax_csi_capture.h"
static AxCsiY y;
static AxCsiUv uv;
int main(void) {
    assert(offsetof(AxCsiY, guard) == AX_CSI_Y_BYTES);
    assert(offsetof(AxCsiUv, guard) == AX_CSI_UV_BYTES);
#ifdef LUMA_BENCH_RUN
    assert(offsetof(AxCsiY, tail) >= 1280u*720u + 1280u);
    assert(offsetof(AxCsiUv, tail) >= 1280u*360u + 1280u);
#endif
    ax_csi_guards_init(&y, &uv);
    assert(ax_csi_capture_faults(&y, &uv) == 0);
    memset((unsigned char *)&y + AX_CSI_Y_BYTES, 0, sizeof(y.guard));
    assert(ax_csi_capture_faults(&y, &uv) == 1);
    uv.guard[7] = 0;
    assert(ax_csi_capture_faults(&y, &uv) == 3);
    y.tail[0] = 0;
    uv.tail[7] = 0;
    assert(ax_csi_capture_faults(&y, &uv) == 15);
    ax_csi_guards_init(&y, &uv);
    assert(ax_csi_capture_faults(&y, &uv) == 0);
    return 0;
}
'''
    file = tmp_path / 'guards.c'
    file.write_text(source)
    binary = tmp_path / 'guards'
    subprocess.run([cc, '-std=c99', '-Wall', '-Wextra', '-Werror',
                    *(['-DLUMA_BENCH_RUN'] if bench else []),
                    '-I', str(ROOT / 'custom-firmware/include'),
                    str(file), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def test_both_entries_apply_exposure_after_bringup():
    source = (ROOT / 'custom-firmware/boards/ax3292.c').read_text()
    main = source[source.index('void firmware_main(void)'):source.index('void luma_bench_once(')]
    bench = source[source.index('void luma_bench_once('):]
    for body, argument in ((main, 'BENCH_PACKET_IN'), (bench, 'in')):
        assert body.index(f'bench_read_options({argument});') < body.index('    hardware_init();')
        assert body.index('    hardware_init();') < body.index(f'bench_apply_exposure({argument});')
    assert source.count('bench_apply_exposure(') == 3  # one definition, two callers


def test_shared_options_use_host_size_word_13(tmp_path):
    cc = shutil.which(os.environ.get('CC', 'cc'))
    if not cc:
        pytest.skip('C compiler unavailable')
    board = (ROOT / 'custom-firmware/boards/ax3292.c').read_text()
    start = board.index('static void bench_read_options(')
    end = board.index('\n}', start) + 2
    source = '''
#include <stdint.h>
#include <assert.h>
#include "stock_photo.h"
#include "ax_jpeg_capture.h"
static u32 luma_test_pattern, luma_overlay_on, luma_csi_size_override, luma_photos_enabled, luma_jpeg_check, luma_jpeg_phases;
''' + board[start:end] + '''
int main(void) {
    u32 in[16] = {0};
    in[7] = 1; in[11] = 1;
    in[12] = 0xdeadbeef;  /* reserved host word must not supply the size */
    in[13] = 0x02cf04ff;
    bench_read_options(in);
    assert(luma_test_pattern == 1 && luma_overlay_on == 1);
    assert(luma_csi_size_override == 0x02cf04ff);
    assert(!luma_photos_enabled);
    in[14] = 1; bench_read_options(in); assert(!luma_photos_enabled);
    in[14] = 0xa5a5a5a5; bench_read_options(in); assert(!luma_photos_enabled);
    in[14] = STOCK_PHOTO_WRITE_TOKEN; bench_read_options(in); assert(luma_photos_enabled);
    assert(!luma_jpeg_check);
    in[15]=0xa5a5a5a5; bench_read_options(in); assert(!luma_jpeg_check);
    in[15]=AX_JPEG_CHECK_TOKEN; bench_read_options(in); assert(luma_jpeg_check);
    assert(luma_jpeg_phases==3);            /* default: encode + decode */
    in[15]=AX_JPEG_CHECK_TOKEN|1; bench_read_options(in); assert(luma_jpeg_phases==1);
    in[15]=AX_JPEG_CHECK_TOKEN|0x1000; bench_read_options(in); assert(!luma_jpeg_check);
    return 0;
}
'''
    file = tmp_path / 'options.c'
    file.write_text(source)
    binary = tmp_path / 'options'
    subprocess.run([cc, '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'custom-firmware/include'),
                    str(file), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def test_normal_frame_uses_stock_callback_sequence(tmp_path):
    cc = shutil.which(os.environ.get('CC', 'cc'))
    if not cc:
        pytest.skip('C compiler unavailable')
    board = (ROOT / 'custom-firmware/boards/ax3292.c').read_text()
    start = board.index('static void csi_service(void)\n{')
    end = board.index('\nstatic void present(', start)
    # Compile the production function, not a second implementation of it.
    service = board[start:end]
    source = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t u32;
#define SEN_Y_BYTES 57600u
#define SEN_UV_BYTES 28800u
#include "ax_csi_capture.h"
static unsigned char sensor_y[SEN_Y_BYTES];
static unsigned char sensor_uv[SEN_UV_BYTES];
static AxCsiY sensor_dma_y;
static AxCsiUv sensor_dma_uv;
static u32 luma_csi_seen, luma_csi_calls, luma_csi_rearms, luma_csi_frame_ends;
static u32 luma_csi_fault;
static int failing;
static u32 regs[0x10000/4];
static unsigned accesses[32], access_count, copies;
static u32 *reg(unsigned n)
{
    assert(access_count < 32);
    accesses[access_count++] = n;
    if (n == 0xb060 || n == 0xb064) assert(copies == 2);
    if (n == 0xb018 && !failing) {
        assert(copies == 2);
        assert(regs[0xb060/4] == (u32)(uintptr_t)sensor_dma_y.data);
        assert(regs[0xb064/4] == (u32)(uintptr_t)sensor_dma_uv.data);
    }
    return &regs[n/4];
}
#define REG(n) (*reg(n))
static void *snapshot_copy(void *dst, const void *src, size_t n)
{
    /* Acknowledge the pending event before copying or kicking the next frame. */
    assert(access_count >= 2);
    assert(accesses[0] == 0xb00c && accesses[1] == 0xb00c);
    copies++;
    return memcpy(dst, src, n);
}
#define memcpy snapshot_copy
''' + service + r'''
#undef memcpy
int main(void)
{
    ax_csi_guards_init(&sensor_dma_y, &sensor_dma_uv);
    regs[0x8980/4] = 0x13579;
    regs[0xb018/4] = 0x200000;
    csi_service();
    assert(access_count == 1 && copies == 0 && luma_csi_frame_ends == 0);
    assert(luma_csi_calls == 0 && luma_csi_rearms == 0);

    access_count = 0;
    regs[0xb00c/4] = 8;  /* sensor-size statistics, not LCD frame end */
    csi_service();
    assert(access_count == 2 && copies == 0 && luma_csi_frame_ends == 0);
    assert(regs[0xb018/4] == 0x200000);

    for (unsigned frame = 1; frame <= 2; frame++) {
        access_count = copies = 0;
        memset(sensor_dma_y.data, 20 + frame, SEN_Y_BYTES);
        memset(sensor_dma_uv.data, 120 + frame, SEN_UV_BYTES);
        regs[0xb060/4] = 0x11111111;  /* stale destination must be replaced */
        regs[0xb064/4] = 0x22222222;
        regs[0xb018/4] = 0x200000;
        regs[0xb00c/4] = 0xc00a;
        csi_service();
        assert(copies == 2 && luma_csi_fault == 0);
        assert(luma_csi_frame_ends == frame && luma_csi_rearms == frame);
        assert(regs[0xb018/4] == 0x240000);
        assert(regs[0x8980/4] == 0x13579);  /* no per-frame PIP reset */
        assert(!memcmp(sensor_dma_y.data, sensor_y, SEN_Y_BYTES));
        assert(!memcmp(sensor_dma_uv.data, sensor_uv, SEN_UV_BYTES));
        memset(sensor_dma_y.data, 0, SEN_Y_BYTES);
        assert(sensor_y[SEN_Y_BYTES-1] == 20 + frame);
    }
    assert(luma_csi_seen == 0xc00a && luma_csi_calls == 3);
    /* A DMA overrun must stop output/core before copying or re-arming. */
    access_count = copies = 0;
    failing = 1;
    sensor_dma_y.guard[0] = 0;
    regs[0xb000/4] = 1;
    csi_service();
    assert(luma_csi_fault == 1 && copies == 0);
    assert(!(regs[0xb018/4] & 0x40000) && !(regs[0xb000/4] & 1));
    assert(luma_csi_rearms == 2);
    return 0;
}
'''
    file = tmp_path / 'csi.c'
    file.write_text(source)
    binary = tmp_path / 'csi'
    subprocess.run([cc, '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-Wno-pointer-to-int-cast', '-fsanitize=undefined',
                    '-I', str(ROOT / 'custom-firmware/include'),
                    str(file), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
