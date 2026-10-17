/* SPDX-License-Identifier: MIT
 * Polled 1280x720 -> 320x180 hardware JPEG decode.
 *
 * The gallery has to show a photo the camera saved at sensor resolution in a
 * 320x180 viewport, so the downscale belongs in the decoder, not in a software
 * resample: ax32xx_mjpegDecodeSetSize takes a source width below 0x1201 and has a
 * 4:1 fast path for exactly 1280 -> 320.
 *
 * Stock surface used, all verified in firmware/original.bin:
 *   mjpeg_parse_image_header            0x02041864  builds the decoder tables
 *   hal_mjpegDecodeInit (const-prop)    0x02041d64  programs the decode block
 *   ax32xx_mjpegDecodeSetSize           0x02036cd8  source/target/stride + scaler
 *   ax32xx_mjpegDecodeStop              0x02037350  clears the block afterwards
 *
 * Deliberately NOT used: ax32xx_mjpegDecodeEnable and hal_mjpegDecodeOneFrame_Ext.
 * Both end in ax32xx_intEnable(0x16, 1), which would unmask the JPEG-decoder
 * interrupt in the stock PICMR while a RAM image owns the machine. The enable is
 * one register write (PRE_JPCON1 |= 0x20000063), so it is inlined instead, exactly
 * as the encoder's stop path already inlines ax32xx_mjpegEncodeEnable(0).
 *
 * Also deliberately not used: ax32xx_mjpegDecodeQDTCfg. The stock parser already
 * materialises the quantiser and Huffman tables into the decoder's fixed table RAM
 * at 0x66400 (via ax32xx_mjpegDecodeInitTable, 796 bytes), and QDTCfg would only
 * re-copy the same tables from the same offsets. Reproducing that 0x66400 layout
 * by hand is the one part of this subsystem that is not worth reimplementing.
 */
#ifndef AX_JPEG_DECODE_H
#define AX_JPEG_DECODE_H
#include "luma.h"
#include "ax_jpeg_capture.h"

#define AX_DECODE_W 320u
#define AX_DECODE_H 180u
#define AX_DECODE_Y_BYTES (AX_DECODE_W*AX_DECODE_H)
#define AX_DECODE_UV_BYTES (AX_DECODE_W*(AX_DECODE_H/2u))
#define AX_DECODE_GUARD 0x4445434au          /* 'JDEC' */
#define AX_DECODE_CHECK_TOKEN 0x4a504744u    /* 'JPDG' */

typedef struct {
    u32 (*read)(unsigned);
    void (*write)(unsigned,u32);
    /* 1 when the stock allocator can supply the parser's 1 KiB scratch block. */
    int (*alloc_ready)(void);
    void (*flush)(const void *address,unsigned bytes);
    int (*parse)(const u8 *jpeg);                          /* 0x02041864 */
    int (*arm)(const u8 *jpeg,u8 *y,u8 *uv,u16 w,u16 h);   /* 0x02041d64 */
    void (*size)(u32 sw,u32 sh,u32 tw,u32 th,u32 stride);  /* 0x02036cd8 */
    void (*stop)(void);                                    /* 0x02037350 */
} AxDecodeOps;

typedef struct {
    const AxDecodeOps *ops;
    u8 *y,*uv;
    volatile u32 *y_tail,*uv_tail;
    u32 started,state,fault,status;
} AxDecode;

enum { AX_DECODE_IDLE, AX_DECODE_RUNNING, AX_DECODE_DONE, AX_DECODE_FAILED };

/* Start decoding one complete JPEG into a 320x180 YUV420 view. Returns 0 when the
 * decoder could not be armed; state and fault then say why. */
int ax_decode_start(AxDecode *d,const AxDecodeOps *ops,const u8 *jpeg,unsigned length,
                    u8 *y,unsigned y_capacity,volatile u32 *y_tail,
                    u8 *uv,unsigned uv_capacity,volatile u32 *uv_tail,u32 now);
void ax_decode_poll(AxDecode *d,u32 now);
#endif
