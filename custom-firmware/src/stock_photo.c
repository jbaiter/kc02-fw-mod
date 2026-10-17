/* SPDX-License-Identifier: MIT
 * RAM-only photo adapter for the researched original KC02 image. The emulator's
 * standalone image retains its existing FAT backend. No raw SD initialization,
 * formatter, unlink or updater is called here.
 *
 * Photos are saved at the sensor's own resolution: the hardware JPEG encoder
 * writes 1280x720 and those bytes are the file. The 320x180 view is produced by
 * the hardware decoder when the photo is opened, so nothing is ever stored or
 * displayed at preview resolution by accident.
 */
#include "stock_photo.h"
#ifdef LUMA_BENCH_RUN
static StockFs fs;
static u8 *staging;
static unsigned staging_capacity;
typedef struct { u8 data[AX_DECODE_Y_BYTES]; u32 guard[8]; } ViewY;
typedef struct { u8 data[AX_DECODE_UV_BYTES]; u32 guard[8]; } ViewUv;
static ViewY view_y __attribute__((aligned(32)));
static ViewUv view_uv __attribute__((aligned(32)));
static const StockPhotoDecode *decoder;
static u16 *viewer_target;
static int feedback_pending;

static int sd_route_ok(void)
{
    return *(volatile u32 *)0x020f2f0cu == 0x0203cf8cu &&
           *(volatile u32 *)0x020f2f18u == 0x0203d05cu;
}
static const StockFsOps original_ops={
    sd_route_ok,
    (int (*)(void *,const char *,u8))0x02070f7cu,
    (int (*)(void *,void *,unsigned,unsigned *))0x02071440u,
    (int (*)(void *,const void *,unsigned,unsigned *))0x0207174cu,
    (int (*)(void *))0x02071d58u,
    (int (*)(void *,const char *))0x020723a0u,
    (int (*)(void *,void *))0x0207250cu,
    (int (*)(void *))0x020724d0u,
    (int (*)(void))0x0203c6e8u
};
void stock_photo_init(int allow_writes, u8 *buffer, unsigned capacity,
                      const StockFsOps *ops, const StockPhotoDecode *decode)
{
    staging=buffer;
    staging_capacity=capacity;
    decoder=decode;
    viewer_target=0;
    feedback_pending=0;
    stock_fs_init(&fs,ops ? ops : &original_ops,allow_writes);
}
unsigned stock_photo_count(void) { return fs.ready ? fs.count : 0; }
const StockFs *stock_photo_state(void) { return &fs; }

u8 *stock_photo_view_y(void) { return view_y.data; }
u8 *stock_photo_view_uv(void) { return view_uv.data; }
volatile u32 *stock_photo_view_y_guard(void) { return view_y.guard; }
volatile u32 *stock_photo_view_uv_guard(void) { return view_uv.guard; }
unsigned photo_yuv_read(const u8 *y, const u8 *uv, u16 *out)
{
    unsigned x,r;
    if (!y || !uv || !out) return 0;
    for (r=0; r<AX_DECODE_H; ++r) {
        const u8 *crow = y + r*AX_DECODE_W;
        const u8 *arow = uv + (r/2)*AX_DECODE_W;
        const u8 *brow = arow + 1;
        for (x=0; x<AX_DECODE_W; ++x) {
            int c = crow[x] - 16, d = arow[x & ~1u] - 128, e = brow[x & ~1u] - 128;
            int rr = (298*c + 409*e + 128) >> 8;
            int gg = (298*c - 100*d - 208*e + 128) >> 8;
            int bb = (298*c + 516*d + 128) >> 8;
            if (rr < 0) rr = 0; else if (rr > 255) rr = 255;
            if (gg < 0) gg = 0; else if (gg > 255) gg = 255;
            if (bb < 0) bb = 0; else if (bb > 255) bb = 255;
            out[r*LW+x] = RGB((unsigned)rr,(unsigned)gg,(unsigned)bb);
        }
    }
    for (r=AX_DECODE_H*LW; r<PIXELS; ++r) out[r]=0;
    return AX_DECODE_H;
}

int stock_photo_save_jpeg(const u8 *jpeg, unsigned length)
{
    int ret;
    if (fs.kind) return PHOTO_BUSY;
    if (!fs.ready || !fs.writable) return 0;
    if (fs.count>=GALLERY_MAX || fs.next_id>99999u) return PHOTO_FULL;
    ret=stock_fs_save(&fs,jpeg,length,STOCK_FS_JPEG);
    if (ret==PHOTO_QUEUED) feedback_pending=1;
    return ret;
}
int stock_photo_load(unsigned index, u16 *pixels)
{
    int ret;
    if (!staging) return 0;
    ret=stock_fs_load(&fs,index,staging,staging_capacity);
    if (ret==PHOTO_QUEUED) {
        viewer_target=pixels;
        feedback_pending=1;
    }
    return ret;
}
int stock_photo_poll(App *a,u32 now,int display_ready)
{
    int event;
    /* Let the first shutter/loading feedback actually latch before starting I/O.
     * Subsequent 512-byte steps need not wait for another display frame. */
    if (feedback_pending) {
        if (!display_ready) return 0;
        feedback_pending=0;
    }
    event=stock_fs_step(&fs);
    if (event==STOCK_FS_SAVED) {
        a->count=fs.count;
        if (a->screen!=GALLERY || fs.pending_load<0) a->photo=a->count-1;
        app_notice(a,a->ink==INK_OFF ? "Photo saved" : "Saved - printer not ready",now);
        return 1;
    }
    if (event==STOCK_FS_LOADED) {
        if (a->screen==GALLERY && a->photo==fs.active_index && viewer_target) {
            unsigned rows=0;
            if (fs.loaded_format==STOCK_FS_JPEG) {
                /* Decode the still the camera saved, straight to view size. */
                if (decoder && decoder->to_view &&
                    decoder->to_view(staging,fs.loaded_size)) {
                    rows=photo_yuv_read(view_y.data,view_uv.data,viewer_target);
                }
            } else {
                rows=photo_bmp_read(staging,fs.loaded_size,viewer_target);
            }
            if (!rows) app_notice(a,"Photo unavailable",now);
            else { a->photo_rows=rows; a->notice[0]=0; }
        }
        return 1;
    }
    if (event==STOCK_FS_FAILED) {
        app_notice(a,"SD I/O failed",now);
        return 1;
    }
    return 0;
}
#endif
