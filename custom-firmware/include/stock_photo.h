/* SPDX-License-Identifier: MIT */
#ifndef LUMA_STOCK_PHOTO_H
#define LUMA_STOCK_PHOTO_H
#include "ax_jpeg_decode.h"
#include "stock_fs.h"
#define STOCK_PHOTO_WRITE_TOKEN 0x53445048u /* explicit host opt-in, packet word 14 */

/* The hardware decode is supplied by the board: real stock routines in a RAM run,
 * a stub in native tests. It fills the adapter's 320x180 YUV view from one
 * complete still, so the adapter owns the planes and only asks for the decode. */
typedef struct {
    int (*to_view)(const u8 *jpeg, unsigned length);
} StockPhotoDecode;

/* RAM-run adapter only. NULL ops selects pinned original-KC02 routines;
 * non-NULL ops permits native tests without any hardware access. `jpeg_capacity`
 * is the staging buffer used both for a queued save and for reading a still back;
 * the encoder's output window serves both because capturing and browsing cannot
 * overlap. */
void stock_photo_init(int allow_writes, u8 *jpeg_staging, unsigned jpeg_capacity,
                      const StockFsOps *ops, const StockPhotoDecode *decode);
unsigned stock_photo_count(void);
/* Queue a create-only save of the encoder's JPEG. The bytes must stay unchanged
 * until the completion event; a queued save blocks further captures. */
int stock_photo_save_jpeg(const u8 *jpeg, unsigned length);
int stock_photo_load(unsigned index, u16 *pixels);
int stock_photo_poll(App *app, u32 now, int display_ready);
const StockFs *stock_photo_state(void);
/* The decoded view planes and their adjacent tail guards. The decoder writes
 * straight into them, and the guards sit immediately after each plane so an
 * overrun is caught rather than silently landing in the next object. */
u8 *stock_photo_view_y(void);
u8 *stock_photo_view_uv(void);
volatile u32 *stock_photo_view_y_guard(void);
volatile u32 *stock_photo_view_uv_guard(void);
/* YUV420 (320x180 / 320x90, 320 stride) to the viewer's RGB rows. */
unsigned photo_yuv_read(const u8 *y, const u8 *uv, u16 *out);
#endif
