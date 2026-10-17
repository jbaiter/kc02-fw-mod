"""Software-level tests for the polled 1280x720 hardware-JPEG trial.

They pin the sequence the stock firmware uses for its manual-encode still
branch: I2W frame DMA into SDRAM, encoder reading those planes directly, manual
trigger on the CSI frame event, and a length/marker check before the bytes are
trusted. They cannot prove silicon behaviour; the bounded hardware trial does.
"""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / 'custom-firmware/include'
SOURCE = ROOT / 'custom-firmware/src/ax_jpeg_capture.c'


def baseline_jpeg(width=1280, height=720, entropy=b'\x11\x22\x33'):
    """A small but structurally real baseline JPEG frame."""
    sof = (b'\xff\xc0' + (17).to_bytes(2, 'big') + b'\x08'
           + height.to_bytes(2, 'big') + width.to_bytes(2, 'big') + b'\x03'
           + b'\x01\x22\x00\x02\x11\x01\x03\x11\x01')
    sos = b'\xff\xda' + (12).to_bytes(2, 'big') + b'\x03\x01\x00\x02\x11\x03\x11\x00\x3f\x00'
    return (b'\xff\xd8' + b'\xff\xe0' + (16).to_bytes(2, 'big') + b'JFIF\x00\x01\x01\x00'
            + b'\x00\x01\x00\x01\x00\x00' + sof + sos + entropy + b'\xff\xd9')


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "ax_csi_capture.h"
#include "ax_jpeg_capture.h"

static const unsigned char framedata[] = {FRAME_BYTES};
#define FRAME_LEN ((unsigned)sizeof(framedata))

static u32 regs[0x10000/4];
static unsigned init_calls, i2w_calls, manual_calls, geometry_calls;
static AxJpegOutput output;
static AxJpeg j;
static unsigned char plane_y[1280*720+512], plane_uv[1280*360+512];
static volatile u32 y_tail[8], uv_tail[8];
static int plant_frame_on_trigger;

static u32 rd(unsigned n) { return regs[n/4]; }
static void wr(unsigned n,u32 v) { regs[n/4]=v; }
static void op_init(int manual,int tp)
{
    assert(manual==1 && tp==0);          /* manual mode, calibration tables */
    init_calls++;
    regs[0xb800/4] |= 0x400;             /* JEMANUAL, as EncodeInit(true) */
}
static void op_i2w(u8 *y,u8 *uv,u16 lines,u16 w)
{
    i2w_calls++;
    assert(lines==720 && w==1280);       /* sensor geometry, never the preview */
    regs[0xb040/4]=(u32)(uintptr_t)y;
    regs[0xb044/4]=(u32)(uintptr_t)uv;
    regs[0xb048/4]=lines;
    regs[0xb04c/4]=(u32)lines*w;
}
static void op_inline(u8 *y,u8 *uv)
{
    regs[0xb820/4]=(u32)(uintptr_t)y;    /* encoder reads the planes directly */
    regs[0xb824/4]=(u32)(uintptr_t)uv;
}
static void op_geometry(u32 sw,u32 sh,u32 tw,u32 th)
{
    geometry_calls++;
    assert(sw==1280 && sh==720 && tw==1280 && th==720);
}
static void op_quality(u8 q) { assert(q==4); }   /* SDK JPEG_Q_75 */
static void op_info(u8 m) { assert(m==0); }      /* plain JPEG, no AVI prefix */
static void op_output(u32 s,u32 e)
{
    assert(s==(u32)(uintptr_t)output.bytes && e-s==AX_JPEG_CAPACITY);
}
static void op_manual(void)
{
    manual_calls++;
    if (plant_frame_on_trigger) {
        memcpy(output.bytes,framedata,FRAME_LEN);
        regs[0xb81c/4]=(u32)(uintptr_t)output.bytes+FRAME_LEN;
        regs[0xb840/4]=0x40;             /* FRAMEEND with the whole frame out */
    }
}
static void op_flush(u32 a,u32 n) { (void)a; (void)n; }
static void op_invalidate(u32 a,u32 n) { (void)a; (void)n; }

static const AxJpegOps ops={rd,wr,op_init,op_i2w,op_inline,op_geometry,op_quality,
                            op_info,op_output,op_manual,op_flush,op_invalidate};

static void reset_world(u32 mode,u32 dma)
{
    unsigned i;
    for (i=0;i<0x10000/4;i++) regs[i]=0;
    regs[0xb000/4]=mode; regs[0xb004/4]=0x42ffff;
    regs[0xb014/4]=0x800ef87b; regs[0xb018/4]=dma;
    for (i=0;i<8;i++) { y_tail[i]=uv_tail[i]=AX_CSI_GUARD; }
    memset(&output,0,sizeof(output));
    init_calls=i2w_calls=manual_calls=geometry_calls=0;
    plant_frame_on_trigger=0;
}

static int arm(void)
{
    return ax_jpeg_start(&j,&ops,&output,plane_y,sizeof(plane_y),y_tail,
                         plane_uv,sizeof(plane_uv),uv_tail,0);
}
'''

CASES = HARNESS + r'''
int main(void)
{
    reset_world(0x0440062d,0x00240000);   /* the verified preview steady state */

    /* 1. A plane that cannot hold a full frame is refused before anything is
     *    reconfigured: photos must never silently become preview-sized. */
    assert(!ax_jpeg_start(&j,&ops,&output,plane_y,320*180,y_tail,
                          plane_uv,320*90,uv_tail,0));
    assert(j.state==AX_JPEG_FAILED && j.fault==1 && init_calls==0);
    assert(regs[0xb000/4]==0x0440062d);

    /* 2. Arming takes the display output away, sets the full-window I2W crop,
     *    keeps the encoder on the captured planes and enables only the I2W DMA. */
    reset_world(0x0440062d,0x00240000);
    assert(arm());
    assert(j.state==AX_JPEG_INPUT);
    assert(init_calls==1 && i2w_calls==1 && geometry_calls==1);
    assert(regs[0xb038/4]==(1280u<<16) && regs[0xb03c/4]==(720u<<16));
    assert(regs[0xb820/4]==(u32)(uintptr_t)plane_y);
    assert(regs[0xb824/4]==(u32)(uintptr_t)plane_uv);
    assert(regs[0xb018/4] & 0x100000);                 /* WIFIEN */
    assert(!(regs[0xb018/4] & 0x80000));               /* no MJPEG stream */
    assert(regs[0xb000/4] & 1);                        /* CSI running */

    /* 3. Without a frame event the encoder stays idle. */
    ax_jpeg_poll(&j,1100);
    assert(manual_calls==0 && j.state==AX_JPEG_INPUT);

    /* 4. The frame event stops the writer and then triggers the encoder, in that
     *    order: encoding from a plane that is still being written would tear. */
    regs[0xb00c/4]=0x200;
    ax_jpeg_poll(&j,1200);
    assert(manual_calls==1 && j.state==AX_JPEG_ENCODING);
    assert(!(regs[0xb018/4] & 0x100000));

    /* 5. Bytes that are not one complete 1280x720 baseline frame are refused,
     *    and the encoder is stopped rather than left running. */
    memset(output.bytes,0x5a,64);
    regs[0xb81c/4]=(u32)(uintptr_t)output.bytes+64;
    regs[0xb840/4]=0x40;
    ax_jpeg_poll(&j,1300);
    assert(j.state==AX_JPEG_FAILED && j.fault==8);
    assert(regs[0xb800/4] & 0x200);                    /* JESTOP */
    assert(!(regs[0xb018/4] & 0x180000));
    return 0;
}
'''

TIMEOUT = HARNESS + r'''
int main(void)
{
    reset_world(0x0440062d,0);
    assert(arm());
    ax_jpeg_poll(&j,2001);            /* stock still photo gives up at 2 s */
    assert(j.state==AX_JPEG_FAILED && j.fault==2);
    return 0;
}
'''

GUARD = HARNESS + r'''
int main(void)
{
    reset_world(0x0440062d,0);
    assert(arm());
    y_tail[3]=0;                      /* the frame overran the capture plane */
    regs[0xb00c/4]=0x200;
    ax_jpeg_poll(&j,10);
    assert(j.state==AX_JPEG_FAILED && j.fault==7);
    assert(manual_calls==0);          /* never encode a truncated plane */
    return 0;
}
'''

FULL = HARNESS + r'''
int main(void)
{
    unsigned char padding[64];
    reset_world(0x0440062d,0);
    assert(arm());
    plant_frame_on_trigger=1;    /* the encoder fills the buffer at trigger */
    regs[0xb00c/4]=0x200;
    ax_jpeg_poll(&j,10);               /* frame event: trigger the encoder */
    assert(j.state==AX_JPEG_ENCODING);
    ax_jpeg_poll(&j,20);               /* encoder reports the finished frame */
    assert(j.state==AX_JPEG_DONE);
    assert(j.length==FRAME_LEN);
    assert(j.fault==0);
    assert(regs[0xb800/4] & 0x200);
    assert(!(regs[0xb018/4] & 0x180000));
    (void)padding;
    return 0;
}
'''

OUTERR = HARNESS + r'''
int main(void)
{
    reset_world(0x0440062d,0);
    assert(arm());
    regs[0xb00c/4]=0x200;
    ax_jpeg_poll(&j,10);
    regs[0xb840/4]=0x8;               /* OUTERR */
    ax_jpeg_poll(&j,20);
    assert(j.state==AX_JPEG_FAILED && j.fault==4);
    return 0;
}
'''

OUTFULL = HARNESS + r'''
int main(void)
{
    reset_world(0x0440062d,0);
    assert(arm());
    regs[0xb00c/4]=0x200;
    ax_jpeg_poll(&j,10);
    regs[0xb840/4]=0x1;               /* OUTPAUSE without FRAMEEND */
    ax_jpeg_poll(&j,20);
    assert(j.state==AX_JPEG_FAILED && j.fault==5);
    return 0;
}
'''

CSI_ERR = HARNESS + r'''
int main(void)
{
    reset_world(0x0440062d,0);
    assert(arm());
    regs[0xb00c/4]=0x10;              /* SEN_STATE_ERR while capturing */
    ax_jpeg_poll(&j,10);
    assert(j.state==AX_JPEG_FAILED && j.fault==3);
    assert(manual_calls==0 && !(regs[0xb000/4] & 1) == 0);
    return 0;
}
'''

BUSY = HARNESS + r'''
int main(void)
{
    reset_world(0x0440062d,0x002c0000);   /* MJPEG stream already enabled */
    assert(!arm());
    assert(j.fault==1);
    reset_world(0x0440062c,0);            /* CSI not running */
    assert(!arm());
    assert(j.fault==1);
    return 0;
}
'''


def _compile_and_run(tmp_path, source):
    cc = shutil.which(os.environ.get('CC', 'cc'))
    if not cc:
        pytest.skip('C compiler unavailable')
    data = ','.join(str(b) for b in baseline_jpeg())
    src = tmp_path / 'case.c'
    src.write_text(source.replace('FRAME_BYTES', data))
    binary = tmp_path / 'case'
    subprocess.run([cc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(INCLUDE),
                    str(src), str(SOURCE), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


@pytest.mark.parametrize('case', [CASES, TIMEOUT, GUARD, FULL, OUTERR, OUTFULL,
                                 CSI_ERR, BUSY],
                         ids=['sequence', 'timeout', 'guard', 'complete', 'outerr',
                              'outfull', 'csi_error', 'precondition'])
def test_jpeg_capture_state_machine(tmp_path, case):
    _compile_and_run(tmp_path, case)


PARSER = r'''
#include <stdio.h>
#include "ax_jpeg_capture.h"
int main(int argc,char **argv)
{
    static unsigned char b[1<<20];
    unsigned len,w,h;
    FILE *f=fopen(argv[1],"rb");
    size_t n=fread(b,1,sizeof(b),f);
    fclose(f);
    if (!ax_jpeg_frame_info(b,(unsigned)n,&len,&w,&h)) { printf("none\n"); return 0; }
    printf("%u %u %u\n",len,w,h);
    return 0;
}
'''


def _parse(tmp_path, data):
    cc = shutil.which(os.environ.get('CC', 'cc'))
    if not cc:
        pytest.skip('C compiler unavailable')
    src = tmp_path / 'parser.c'
    src.write_text(PARSER)
    binary = tmp_path / 'parser'
    subprocess.run([cc, '-std=c99', '-w', '-I', str(INCLUDE), str(src), str(SOURCE),
                    '-o', str(binary)], check=True)
    frame = tmp_path / 'frame.bin'
    frame.write_bytes(data)
    out = subprocess.run([str(binary), str(frame)], capture_output=True,
                         text=True, check=True).stdout.split()
    return None if out[0] == 'none' else tuple(int(v) for v in out)


def test_parser_reports_exact_length_and_ignores_trailing_padding(tmp_path):
    frame = baseline_jpeg()
    assert _parse(tmp_path, frame) == (len(frame), 1280, 720)
    assert _parse(tmp_path, frame + bytes(64)) == (len(frame), 1280, 720)


def test_parser_tolerates_entropy_stuffing_and_restart_markers(tmp_path):
    frame = baseline_jpeg(entropy=b'\x00\xff\x00\x12\xff\xd0\x34\xff\x00\x56')
    assert _parse(tmp_path, frame) == (len(frame), 1280, 720)


def test_parser_rejects_truncated_progressive_and_foreign_data(tmp_path):
    frame = baseline_jpeg()
    assert _parse(tmp_path, frame[:-2]) is None                  # no EOI
    assert _parse(tmp_path, b'not a jpeg at all') is None
    progressive = bytearray(frame)
    progressive[progressive.index(b'\xff\xc0') + 1] = 0xc2
    assert _parse(tmp_path, bytes(progressive)) is None
    short = baseline_jpeg(width=640, height=480)
    assert _parse(tmp_path, short) == (len(short), 640, 480)     # caller checks size
