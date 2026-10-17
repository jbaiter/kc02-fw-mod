/* SPDX-License-Identifier: MIT */
#include "ax_jpeg_capture.h"
#include "ax_csi_capture.h"

/* The target is 32-bit; the host test build is not, so pointer values go through
 * unsigned long before narrowing. */
static u32 addr_of(const void *p) { return (u32)(unsigned long)p; }

/* Micro-conditions that make a failed trial diagnosable over USB instead of
 * merely "no JPEG". Codes are reported through AxJpeg.fault. */
enum {
    F_NONE = 0,
    F_PRECONDITION = 1,   /* caller gave the wrong planes or stock CSI is busy */
    F_TIMEOUT = 2,        /* encoder never finished inside the stock 2 s budget */
    F_CSI_EVENT = 3,      /* CSI reported a frame error while capturing */
    F_ENCODER = 4,        /* encoder reported a stream error */
    F_OUTPUT_FULL = 5,    /* 512 KiB output ran out before the frame ended */
    F_LENGTH = 6,         /* OUTLOAD outside the declared buffer */
    F_GUARD = 7,          /* capture plane overran its tail guard */
    F_NOT_JPEG = 8,       /* bytes are not one complete 1280x720 baseline JPEG */
    F_NO_SOURCE = 9,      /* frame end arrived while still waiting for input */
};

int ax_jpeg_frame_info(const u8 *b,unsigned n,unsigned *length,
                       unsigned *width,unsigned *height)
{
    unsigned p=2,w=0,h=0;
    int in_scan=0;
    if (n<4 || b[0]!=0xff || b[1]!=0xd8) return 0;
    while (p<n) {
        unsigned marker,size;
        if (!in_scan) {
            if (b[p]!=0xff) return 0;
            while (p<n && b[p]==0xff) ++p;
            if (p==n) return 0;
            marker=b[p++];
        } else {
            /* Entropy-coded data: 0xff is either stuffing, a restart marker or
             * the next real marker. Anything else after 0xff is corruption. */
            if (b[p]!=0xff) { ++p; continue; }
            while (p<n && b[p]==0xff) ++p;
            if (p==n) return 0;
            marker=b[p++];
            if (marker==0x00 || (marker>=0xd0 && marker<=0xd7)) continue;
        }
        if (marker==0xd9) {
            if (!in_scan || !w || !h) return 0;
            *length=p; *width=w; *height=h; return 1;
        }
        if (marker==0xd8 || marker==0x01 || (marker>=0xd0 && marker<=0xd7)) return 0;
        if (p+2>n) return 0;
        size=(unsigned)b[p]*256u+b[p+1];
        if (size<2 || size>n-p) return 0;
        if (marker==0xc0) {
            if (size<8 || b[p+2]!=8) return 0;
            h=(unsigned)b[p+3]*256u+b[p+4];
            w=(unsigned)b[p+5]*256u+b[p+6];
        } else if (marker>=0xc1 && marker<=0xcf && marker!=0xc4 && marker!=0xc8 && marker!=0xcc) {
            return 0;   /* hardware output is baseline; refuse anything else */
        } else if (marker==0xda) {
            if (!w || !h) return 0;
            in_scan=1;
        }
        p+=size;
    }
    return 0;
}

/* Exactly the register effect of stock ax32xx_mjpegEncodeEnable(0), minus
 * ax32xx_intEnable: this image must not re-point the stock interrupt mask. */
void ax_jpeg_stop(AxJpeg *j)
{
    const AxJpegOps *o=j->ops;
    if (!o || !j->armed) return;
    o->write(0xb800,o->read(0xb800)|0x200u);        /* JESTOP */
    o->write(0xb83c,o->read(0xb83c)&~0x800u);        /* encoder IRQ enable off */
    o->write(0xb800,o->read(0xb800)|0x200u);
    o->write(0xb018,o->read(0xb018)&~0x180000u);     /* MJPGEN + WIFIEN off */
    o->write(0xb8b0,0xffffffffu);                    /* clear encoder flags */
    o->write(0xb014,o->read(0xb014)&~0x60000u);
}

static void fail(AxJpeg *j,u32 fault)
{
    ax_jpeg_stop(j);
    j->fault=fault;
    j->state=AX_JPEG_FAILED;
}

int ax_jpeg_start(AxJpeg *j,const AxJpegOps *o,AxJpegOutput *out,
                  u8 *y,unsigned yc,volatile u32 *yt,
                  u8 *uv,unsigned uvc,volatile u32 *ut,u32 now)
{
    unsigned i;
    *j=(AxJpeg){0};
    j->ops=o; j->out=out; j->y=y; j->uv=uv;
    j->y_capacity=yc; j->uv_capacity=uvc; j->y_tail=yt; j->uv_tail=ut;
    /* A full YUV420 frame is Y + Y/2. Refuse anything that cannot hold it: the
     * whole point of this path is that no downscaled frame is ever encoded. */
    if (!o || !out || yc<AX_JPEG_W*AX_JPEG_H || uvc<AX_JPEG_W*AX_JPEG_H/2u) {
        j->fault=F_PRECONDITION; j->state=AX_JPEG_FAILED; return 0;
    }
    /* Stock CSI must be alive (b000 bit0) and not already streaming MJPEG. */
    if (!(o->read(0xb000)&1u) || (o->read(0xb018)&0x180000u)) {
        j->fault=F_PRECONDITION; j->state=AX_JPEG_FAILED; return 0;
    }
    j->saved_mode=o->read(0xb000);
    j->saved_isp=o->read(0xb004);
    j->saved_inten=o->read(0xb014);
    j->saved_dma=o->read(0xb018);
    j->saved_i2w_h=o->read(0xb038);
    j->saved_i2w_v=o->read(0xb03c);
    j->armed=1;

    /* Take the display output away first: the panel path and the I2W path share
     * the CSI DMA engine, and the stock scaler is about to be reconfigured. */
    o->write(0xb018,j->saved_dma&~0x1c0000u);
    o->write(0xb000,j->saved_mode&~1u);

    for (i=0;i<8;i++) {
        out->before[i]=out->after[i]=AX_JPEG_GUARD;
        yt[i]=ut[i]=AX_CSI_GUARD;
    }
    o->flush(addr_of(y),yc);
    o->flush(addr_of(uv),uvc);
    o->flush(addr_of(out),sizeof(*out));

    /* Manual mode: one frame in, one frame encoded, driven by us. The stock
     * no-downscale branch streams instead, but it needs the ISR-driven ring
     * buffer that this image deliberately does not run. */
    o->init(1,0);
    /* Full-window I2W crop with zero scale-down steps. This is the body of
     * ax32xx_hvScaler for a 1:1 capture, which the stock helper refuses to
     * express because it only accepts integer downscale ratios. */
    o->write(0xb038,AX_JPEG_W<<16);
    o->write(0xb03c,AX_JPEG_H<<16);
    o->i2w(y,uv,(u16)AX_JPEG_H,(u16)AX_JPEG_W);
    o->inline_input(y,uv);
    o->geometry(AX_JPEG_W,AX_JPEG_H,AX_JPEG_W,AX_JPEG_H);
    o->quality(4);   /* SDK JPEG_Q_75 */
    o->info(0);      /* plain JPEG, no AVI index prefix */
    o->output(addr_of(out->bytes),addr_of(out->bytes)+AX_JPEG_CAPACITY);
    o->write(0xb8b0,0xffffffffu);
    o->write(0xb00c,o->read(0xb00c));            /* drop stale CSI flags */
    o->write(0xb018,o->read(0xb018)|0x100000u);  /* WIFIEN: the I2W frame DMA */
    o->write(0xb000,o->read(0xb000)|1u);
    j->started=now;
    j->state=AX_JPEG_INPUT;
    return 1;
}

void ax_jpeg_poll(AxJpeg *j,u32 now)
{
    const AxJpegOps *o=j->ops;
    u32 st,loaded,base;
    unsigned i,n,w,h;
    if (j->state!=AX_JPEG_INPUT && j->state!=AX_JPEG_ENCODING) return;
    if (now-j->started>2000u) { fail(j,F_TIMEOUT); return; }

    st=o->read(0xb00c);
    j->csi_events|=st;
    if (st) o->write(0xb00c,st);
    if (st&0x10u) { fail(j,F_CSI_EVENT); return; }   /* SEN_STATE_ERR */

    if (j->state==AX_JPEG_INPUT) {
        /* Bit 9 is where stock registers the handler that only calls
         * ax32xx_mjpeg_manual_on: the I2W frame is complete in SDRAM. */
        if (!(st&0x200u)) return;
        o->write(0xb018,o->read(0xb018)&~0x100000u);  /* stop the writer */
        for (i=0;i<8;i++) {
            if (j->y_tail[i]!=AX_CSI_GUARD || j->uv_tail[i]!=AX_CSI_GUARD) {
                fail(j,F_GUARD); return;
            }
        }
        o->flush(addr_of(j->y),j->y_capacity);
        o->flush(addr_of(j->uv),j->uv_capacity);
        o->write(0xb8b0,0xffffffffu);
        o->manual_on();
        j->state=AX_JPEG_ENCODING;
        return;
    }

    st=o->read(0xb840);
    j->jpeg_events|=st;
    if (st&0x8u) { fail(j,F_ENCODER); return; }        /* OUTERR */
    if ((st&1u) && !(st&0x40u)) { fail(j,F_OUTPUT_FULL); return; }
    if (!(st&0x40u)) return;                            /* FRAMEEND */

    loaded=o->read(0xb81c);
    if (!loaded) loaded=o->read(0xb818);
    ax_jpeg_stop(j);
    base=addr_of(j->out->bytes);
    if (loaded<=base || loaded-base>AX_JPEG_CAPACITY) { fail(j,F_LENGTH); return; }
    o->invalidate(addr_of(j->out),sizeof(*j->out));
    o->invalidate(addr_of(j->y),j->y_capacity);
    o->invalidate(addr_of(j->uv),j->uv_capacity);
    for (i=0;i<8;i++) {
        if (j->out->before[i]!=AX_JPEG_GUARD || j->out->after[i]!=AX_JPEG_GUARD) {
            fail(j,F_GUARD); return;
        }
    }
    if (!ax_jpeg_frame_info(j->out->bytes,loaded-base,&n,&w,&h) ||
        w!=AX_JPEG_W || h!=AX_JPEG_H) {
        fail(j,F_NOT_JPEG); return;
    }
    j->length=n;
    j->state=AX_JPEG_DONE;
}
