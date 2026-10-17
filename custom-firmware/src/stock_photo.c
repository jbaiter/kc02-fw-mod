/* SPDX-License-Identifier: MIT
 * RAM-only photo adapter for the researched original KC02 image. The emulator's
 * standalone image retains its existing FAT backend. No raw SD initialization,
 * formatter, unlink or updater is called here.
 */
#include "stock_photo.h"
#ifdef LUMA_BENCH_RUN
static StockFs fs;
static u8 encoded[54u+LW*CAMERA_ROWS*3u] __attribute__((aligned(32)));
static u8 *read_bytes;
static unsigned read_capacity;
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
                      const StockFsOps *ops)
{
    read_bytes=buffer;
    read_capacity=capacity;
    viewer_target=0;
    feedback_pending=0;
    stock_fs_init(&fs,ops ? ops : &original_ops,allow_writes);
}
unsigned stock_photo_count(void) { return fs.ready ? fs.count : 0; }
const StockFs *stock_photo_state(void) { return &fs; }
int stock_photo_save(const u16 *pixels)
{
    unsigned n;
    int ret;
    if (fs.kind) return PHOTO_BUSY;
    if (!fs.ready || !fs.writable) return 0;
    if (fs.count>=GALLERY_MAX || fs.next_id>99999u) return PHOTO_FULL;
    n=photo_bmp_write(pixels,encoded,sizeof(encoded));
    if (!n) return 0;
    ret=stock_fs_save(&fs,encoded,n);
    if (ret==PHOTO_QUEUED) feedback_pending=1;
    return ret;
}
int stock_photo_load(unsigned index,u16 *pixels)
{
    int ret=stock_fs_load(&fs,index,read_bytes,read_capacity);
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
            unsigned rows=photo_bmp_read(read_bytes,fs.loaded_size,viewer_target);
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
