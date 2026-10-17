"""Software-level tests for the polled 1280x720 -> 320x180 hardware decode.

The decoder is what lets a full-resolution photo appear in the viewport without a
software resample, so these pin the arming sequence and every refusal. Silicon
behaviour still needs the bounded hardware trial.
"""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

from test_luma_jpeg import baseline_jpeg

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / 'custom-firmware/include'
CAPTURE = ROOT / 'custom-firmware/src/ax_jpeg_capture.c'
DECODE = ROOT / 'custom-firmware/src/ax_jpeg_decode.c'

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "ax_csi_capture.h"
#include "ax_jpeg_decode.h"

static const unsigned char framedata[] = {FRAME_BYTES};
#define FRAME_LEN ((unsigned)sizeof(framedata))

static u32 regs[0x10000/4];
static unsigned written[64], nwritten;
static unsigned parse_calls, arm_calls, size_calls, stop_calls, flush_calls;
static u32 size_args[5];
static int alloc_ok = 1;
static int parse_result;

static u32 rd(unsigned n) { return regs[n/4]; }
static void wr(unsigned n,u32 v)
{
    assert(nwritten < 64);
    written[nwritten++]=n;
    regs[n/4]=v;
}
static int alloc_ready(void) { return alloc_ok; }
static void flush(const void *a,unsigned n) { (void)a; (void)n; flush_calls++; }
static int parse(const u8 *jpeg)
{
    assert(jpeg==framedata);
    parse_calls++;
    return parse_result;
}
static int arm(const u8 *jpeg,u8 *y,u8 *uv,u16 w,u16 h)
{
    assert(jpeg==framedata && w==320 && h==180);
    arm_calls++;
    regs[0xc010/4]=(u32)(uintptr_t)y;      /* DecodeOutputSet */
    regs[0xc014/4]=(u32)(uintptr_t)uv;
    regs[0xc000/4]|=0x60000023u;           /* DecodeInit's mode bits */
    return 0;
}
static void size(u32 sw,u32 sh,u32 tw,u32 th,u32 stride)
{
    size_calls++;
    size_args[0]=sw; size_args[1]=sh; size_args[2]=tw; size_args[3]=th;
    size_args[4]=stride;
}
static void stop(void) { stop_calls++; regs[0xc000/4]=0; }

static const AxDecodeOps ops={rd,wr,alloc_ready,flush,parse,arm,size,stop};

static unsigned char view_y[AX_DECODE_Y_BYTES+512];
static unsigned char view_uv[AX_DECODE_UV_BYTES+512];
static volatile u32 y_tail[8], uv_tail[8];
static AxDecode d;

static void arm_view(void)
{
    unsigned i;
    for (i=0;i<8;i++) { y_tail[i]=uv_tail[i]=AX_CSI_GUARD; }
    memset(view_y,0x2a,sizeof(view_y));
    memset(view_uv,0x55,sizeof(view_uv));
}

static int start_decode(void)
{
    return ax_decode_start(&d,&ops,framedata,FRAME_LEN,
                           view_y,sizeof(view_y),y_tail,
                           view_uv,sizeof(view_uv),uv_tail,0);
}
'''

CASES = HARNESS + r'''
int main(void)
{
    arm_view();
    assert(start_decode());
    assert(d.state==AX_DECODE_RUNNING);
    assert(parse_calls==1 && arm_calls==1 && size_calls==1);
    /* 1280x720 source, 320x180 tightly packed view. */
    assert(size_args[0]==1280 && size_args[1]==720);
    assert(size_args[2]==320 && size_args[3]==180 && size_args[4]==320);
    /* The decoder is aimed at the view planes. */
    assert(regs[0xc010/4]==(u32)(uintptr_t)view_y);
    assert(regs[0xc014/4]==(u32)(uintptr_t)view_uv);
    /* Kick without touching the stock interrupt mask. */
    assert(regs[0xc000/4] & 0x20000063u);
    {
        unsigned i;
        for (i=0;i<nwritten;i++) assert(written[i]!=0x4800);
    }
    ax_decode_poll(&d,10);
    assert(d.state==AX_DECODE_RUNNING && stop_calls==0);
    regs[0xc040/4]=0x40;                    /* decode end */
    ax_decode_poll(&d,20);
    assert(d.state==AX_DECODE_DONE && d.fault==0);
    assert(stop_calls==1);
    return 0;
}
'''

ERROR = HARNESS + r'''
int main(void)
{
    arm_view();
    assert(start_decode());
    regs[0xc040/4]=0x3120;                  /* the stock error bits */
    ax_decode_poll(&d,10);
    assert(d.state==AX_DECODE_FAILED && d.fault==4);
    assert(stop_calls==1);                  /* never left running */
    return 0;
}
'''

TIMEOUT = HARNESS + r'''
int main(void)
{
    arm_view();
    assert(start_decode());
    ax_decode_poll(&d,2001);
    assert(d.state==AX_DECODE_FAILED && d.fault==3);
    return 0;
}
'''

GUARD = HARNESS + r'''
int main(void)
{
    arm_view();
    assert(start_decode());
    y_tail[5]=0;                            /* the view overran its arena */
    regs[0xc040/4]=0x40;
    ax_decode_poll(&d,10);
    assert(d.state==AX_DECODE_FAILED && d.fault==5);
    return 0;
}
'''

EMPTY = HARNESS + r'''
int main(void)
{
    arm_view();
    assert(start_decode());
    memset(view_y,0,AX_DECODE_Y_BYTES);     /* decoder said done, wrote nothing */
    regs[0xc040/4]=0x40;
    ax_decode_poll(&d,10);
    assert(d.state==AX_DECODE_FAILED && d.fault==6);
    return 0;
}
'''

REFUSALS = HARNESS + r'''
int main(void)
{
    /* too small a view */
    arm_view();
    assert(!ax_decode_start(&d,&ops,framedata,FRAME_LEN,
                            view_y,320*90,y_tail,view_uv,320*45,uv_tail,0));
    assert(d.fault==1 && parse_calls==0);

    /* no scratch block for the parser: refuse before calling it */
    arm_view();
    parse_calls=arm_calls=0;
    alloc_ok=0;
    assert(!start_decode());
    assert(d.fault==1 && parse_calls==0);
    alloc_ok=1;

    /* a header the parser rejects */
    arm_view();
    parse_calls=arm_calls=size_calls=stop_calls=0;
    parse_result=-4;
    assert(!start_decode());
    assert(d.fault==2 && arm_calls==0);

    /* truncated input is refused by the marker scan before any register work */
    arm_view();
    parse_calls=arm_calls=size_calls=stop_calls=0;
    assert(!ax_decode_start(&d,&ops,framedata,FRAME_LEN-2,
                            view_y,sizeof(view_y),y_tail,
                            view_uv,sizeof(view_uv),uv_tail,0));
    assert(d.fault==2 && parse_calls==0);
    return 0;
}
'''

OTHER_SIZE = HARNESS + r'''
int main(void)
{
    (void)start_decode;
    /* Only the camera's own 1280x720 stills are accepted; a 640x480 JPEG in the
     * root directory must not be decoded into a 320x180 view. */
    static const unsigned char small[] = {SMALL_BYTES};
    arm_view();
    assert(!ax_decode_start(&d,&ops,small,sizeof(small),
                            view_y,sizeof(view_y),y_tail,
                            view_uv,sizeof(view_uv),uv_tail,0));
    assert(d.fault==2 && parse_calls==0);
    return 0;
}
'''


def _run(tmp_path, source, small=None):
    cc = shutil.which(os.environ.get('CC', 'cc'))
    if not cc:
        pytest.skip('C compiler unavailable')
    text = source.replace('FRAME_BYTES', ','.join(str(b) for b in baseline_jpeg()))
    if small is not None:
        text = text.replace('SMALL_BYTES', ','.join(str(b) for b in small))
    src = tmp_path / 'decode_case.c'
    src.write_text(text)
    binary = tmp_path / 'decode_case'
    subprocess.run([cc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(INCLUDE),
                    str(src), str(DECODE), str(CAPTURE), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


@pytest.mark.parametrize('case', [CASES, ERROR, TIMEOUT, GUARD, EMPTY, REFUSALS],
                         ids=['complete', 'status_error', 'timeout', 'guard',
                              'empty_view', 'refusals'])
def test_decode_state_machine(tmp_path, case):
    _run(tmp_path, case)


def test_decode_refuses_a_photo_that_is_not_sensor_resolution(tmp_path):
    _run(tmp_path, OTHER_SIZE, small=baseline_jpeg(width=640, height=480))


def test_decode_does_not_use_the_stock_interrupt_enabling_entry_points():
    """ax32xx_mjpegDecodeEnable and OneFrame_Ext both end in intEnable(0x16, 1).

    Neither may appear in the decode path: the wrapper asserts that in C, and this
    keeps the source honest about it.
    """
    text = DECODE.read_text()
    for forbidden in ('0x0203727c', '0x020423a4', '0x02042498', '0x02037490'):
        assert forbidden not in text
