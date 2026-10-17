/* SPDX-License-Identifier: MIT */
#include "ax_jpeg_decode.h"
#include "ax_csi_capture.h"

/* Fault codes are reported through AxDecode.fault so a failed gallery load is
 * diagnosable instead of merely "no picture". */
enum {
    D_NONE = 0,
    D_BUFFER = 1,      /* view planes too small, or no scratch for the parser */
    D_HEADER = 2,      /* not a usable 1280x720 baseline JPEG */
    D_TIMEOUT = 3,     /* decoder never signalled completion */
    D_STATUS = 4,      /* decoder raised one of the stock error bits */
    D_GUARD = 5,       /* the view planes overran their tail guards */
    D_EMPTY = 6,       /* completion with a blank plane: nothing was decoded */
};

/* 0xc040: bit 6 is decode end, 0x3120 are the error bits
 * (mjpeg_decode_wait_done at 0x02037ffc tests exactly these, in this order). */
#define DEC_STATUS_DONE   0x40u
#define DEC_STATUS_ERROR  0x3120u
#define DEC_STATUS_BUSY   0x30000u
#define DEC_KICK          0x20000063u   /* PRE_JPCON1 |= this, from DecodeEnable */

static void fail(AxDecode *d,u32 fault)
{
    if (d->ops && d->ops->stop) d->ops->stop();
    d->fault=fault;
    d->state=AX_DECODE_FAILED;
}

int ax_decode_start(AxDecode *d,const AxDecodeOps *o,const u8 *jpeg,unsigned length,
                    u8 *y,unsigned yc,volatile u32 *yt,
                    u8 *uv,unsigned uvc,volatile u32 *ut,u32 now)
{
    unsigned i,w,h;
    int ret;
    *d=(AxDecode){0};
    d->ops=o; d->y=y; d->uv=uv; d->y_tail=yt; d->uv_tail=ut;
    if (!o || !jpeg || yc<AX_DECODE_Y_BYTES || uvc<AX_DECODE_UV_BYTES) {
        d->fault=D_BUFFER; d->state=AX_DECODE_FAILED; return 0;
    }
    /* Only what the camera itself writes: one complete baseline frame at the
     * sensor's resolution. A file that is not that is not a photo from here. */
    if (!ax_jpeg_frame_info(jpeg,length,&length,&w,&h) ||
        w!=AX_JPEG_W || h!=AX_JPEG_H) {
        d->fault=D_HEADER; d->state=AX_DECODE_FAILED; return 0;
    }
    if (!o->alloc_ready()) { d->fault=D_BUFFER; d->state=AX_DECODE_FAILED; return 0; }
    for (i=0;i<AX_CSI_GUARD_WORDS;++i) { yt[i]=ut[i]=AX_CSI_GUARD; }
    /* The bytes came from a file, so the cache may still hold them; the decoder
     * reads memory directly. */
    o->flush(jpeg,length);
    o->flush(y,yc);
    o->flush(uv,uvc);
    ret=o->parse(jpeg);
    if (ret<0) { d->fault=D_HEADER; d->state=AX_DECODE_FAILED; return 0; }
    /* Programs PRE_JPCON1 for the parsed format, the DC timing, the output/input
     * pointers, the restart interval and the component table word. */
    ret=o->arm(jpeg,y,uv,(u16)AX_DECODE_W,(u16)AX_DECODE_H);
    if (ret<0) { d->fault=D_HEADER; d->state=AX_DECODE_FAILED; return 0; }
    /* The constant-propagated init has its stride argument fixed at 0; the real
     * stride belongs here, because the view is tightly packed at 320 bytes. */
    o->size(AX_JPEG_W,AX_JPEG_H,AX_DECODE_W,AX_DECODE_H,AX_DECODE_W);
    o->write(0xc000,o->read(0xc000)|DEC_KICK);
    d->started=now;
    d->state=AX_DECODE_RUNNING;
    return 1;
}

void ax_decode_poll(AxDecode *d,u32 now)
{
    u32 st;
    unsigned i,distinct,zero;
    if (d->state!=AX_DECODE_RUNNING) return;
    st=d->ops->read(0xc040);
    d->status=st;
    if (st&DEC_STATUS_ERROR) { fail(d,D_STATUS); return; }
    if (!(st&DEC_STATUS_DONE)) {
        if (now-d->started>2000u) fail(d,D_TIMEOUT);
        return;
    }
    d->ops->stop();
    for (i=0;i<AX_CSI_GUARD_WORDS;++i) {
        if (d->y_tail[i]!=AX_CSI_GUARD || d->uv_tail[i]!=AX_CSI_GUARD) {
            fail(d,D_GUARD); return;
        }
    }
    /* A completion signal on its own is not proof: a plane that is still the
     * pre-fill pattern means the decoder never wrote the view. */
    zero=0;
    for (i=0;i<AX_DECODE_Y_BYTES;++i) if (!d->y[i]) ++zero;
    distinct = zero==AX_DECODE_Y_BYTES ? 1u : 0u;
    if (distinct) { fail(d,D_EMPTY); return; }
    d->state=AX_DECODE_DONE;
}
