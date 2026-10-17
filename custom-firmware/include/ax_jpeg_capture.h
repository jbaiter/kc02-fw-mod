/* SPDX-License-Identifier: MIT
 * Polled, bounded original-KC02 still capture at the sensor's own 1280x720.
 *
 * Why this shape: the stock firmware has two still-photo branches in
 * FUN_02040c6c, and only the second one is ISR-driven streaming. The first one
 * (manual encode) is a single-shot design: the CSI/ISP writes one complete YUV
 * frame into memory through the I2W DMA, and the hardware JPEG encoder then
 * reads those two planes from SDRAM (JPIBAS..JPIEND) when it is triggered.
 * Nothing is copied, nothing is staged, and no interrupt is needed -- which is
 * what makes it usable from a polled RAM image. Stock triggers it from the CSI
 * VXY_FRAME_EN handler (0x0204062c) which only calls ax32xx_mjpeg_manual_on, so
 * waiting for that CSI event and calling the same function is equivalent.
 *
 * Registers and helpers referenced here were read from firmware/original.bin,
 * not assumed from the SDK: ax32xx_mjpegEncodeInit 0x02036964,
 * ax32xx_csihv_tomjp_dma_cfg 0x02034308, ax32xx_mjpeg_inlinebuf_init 0x02036948,
 * ax32xx_mjpegEncodeSizeSet 0x020366fc, ax32xx_mjpegEncodeQuilitySet 0x020367a4,
 * ax32xx_mjpegEncodeInfoSet 0x020368e4, ax32xx_mjpegEncodeBufferSet 0x020364d0,
 * ax32xx_mjpeg_manual_on 0x02036b9c.
 */
#ifndef AX_JPEG_CAPTURE_H
#define AX_JPEG_CAPTURE_H
#include "luma.h"

#define AX_JPEG_W 1280u
#define AX_JPEG_H 720u
#define AX_JPEG_CAPACITY 0x80000u   /* same 512 KiB output buffer as stock stills */
#define AX_JPEG_GUARD 0x4a504547u
#define AX_JPEG_CHECK_TOKEN 0x4a504743u

/* Full-frame Y/UV capture planes with guards. A full-resolution DMA write
 * legitimately runs past the preview-sized guard, so only the tails are proof. */
typedef struct {
    u32 before[8];
    u8 bytes[AX_JPEG_CAPACITY];
    u32 after[8];
} AxJpegOutput;

typedef struct {
    u32 (*read)(unsigned);
    void (*write)(unsigned,u32);
    void (*init)(int manual,int test_pattern);   /* 0x02036964 */
    void (*i2w)(u8 *y,u8 *uv,u16 lines,u16 width); /* 0x02034308 */
    void (*inline_input)(u8 *y,u8 *uv);          /* 0x02036948 */
    void (*geometry)(u32 sw,u32 sh,u32 tw,u32 th); /* 0x020366fc */
    void (*quality)(u8 q);                       /* 0x020367a4 */
    void (*info)(u8 mode);                       /* 0x020368e4 */
    void (*output)(u32 start,u32 end);           /* 0x020364d0 */
    void (*manual_on)(void);                     /* 0x02036b9c */
    void (*flush)(u32 addr,u32 bytes);
    void (*invalidate)(u32 addr,u32 bytes);
} AxJpegOps;

typedef struct {
    const AxJpegOps *ops;
    AxJpegOutput *out;
    u8 *y,*uv;
    u32 y_capacity,uv_capacity;
    volatile u32 *y_tail,*uv_tail;
    u32 armed,started,state,fault,csi_events,jpeg_events,length;
    u32 saved_mode,saved_isp,saved_inten,saved_dma,saved_i2w_h,saved_i2w_v;
} AxJpeg;

enum { AX_JPEG_IDLE, AX_JPEG_INPUT, AX_JPEG_ENCODING, AX_JPEG_DONE, AX_JPEG_FAILED };

/* Bounded baseline-JPEG parser: exact frame length plus SOF dimensions. */
int ax_jpeg_frame_info(const u8 *data,unsigned available,unsigned *length,
                       unsigned *width,unsigned *height);
/* Returns 0 when the encoder could not be armed; state then explains why. */
int ax_jpeg_start(AxJpeg *j,const AxJpegOps *ops,AxJpegOutput *out,
                  u8 *y,unsigned y_capacity,volatile u32 *y_tail,
                  u8 *uv,unsigned uv_capacity,volatile u32 *uv_tail,u32 now);
void ax_jpeg_poll(AxJpeg *j,u32 now);
/* Stops the encoder and releases the CSI/ISP back to whoever called start. */
void ax_jpeg_stop(AxJpeg *j);
#endif
