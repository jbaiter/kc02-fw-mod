/* SPDX-License-Identifier: MIT
 * QEMU bring-up HAL. Register contracts: vendor AX3292 SDK ax329x.h and
 * docs/BOARD_RE.md. This is NOT a silicon-ready cold boot or printer driver.
 * Missing: clocks/SDRAM tuning, pinmux, panel timing/commands, sensor tables,
 * USB controller and exception/interrupt entry. Do not flash this RAM image.
 */
#include "luma.h"
#include "storage.h"
#include "input.h"
#include "sd_hw.h"
#ifndef BOARD_ID
#error BOARD_ID must be 0 (original), 1 (x8d) or 2 (z12)
#endif
#define REG(n) (*(volatile __sfr u32 *)(n))
static u16 frame[PIXELS], camera[PIXELS], photo[PIXELS];
static u16 theme_bg[PIXELS], theme_boot[PIXELS];
static u8 bitmap[54+PIXELS*3];
static u8 lcd_y[PIXELS] __attribute__((aligned(32)));
static u8 lcd_uv[PIXELS/2] __attribute__((aligned(32)));
static u8 sensor_y[PIXELS] __attribute__((aligned(32)));
static u8 sensor_uv[PIXELS/2] __attribute__((aligned(32)));
static u8 dma[512] __attribute__((aligned(32)));
static SdHw sd;
static Fat fs;
static unsigned photo_ids[GALLERY_MAX], count;
/* GDB observation points; application state is never supplied by the host. */
volatile App luma_app;
volatile u32 luma_frames;

void *memcpy(void *dest, const void *src, size_t n)
{
    u8 *d=dest; const u8 *s=src;
    while (n--) { *d++=*s++; }
    return dest;
}
void *memset(void *dest, int v, size_t n)
{
    u8 *d=dest;
    while (n--) { *d++=(u8)v; }
    return dest;
}
static int read_sector(u32 lba, u8 *out) { return sd_hw_read(&sd, lba, out); }
static int write_sector(u32 lba, const u8 *in) { return sd_hw_write(&sd, lba, in); }
static int read_file(const char *name, u8 *out, unsigned cap, unsigned *n)
{
    return fat_read(&fs,name,out,cap,n);
}
static unsigned photo_count(void)
{
    count=fat_photos(&fs,photo_ids,GALLERY_MAX);
    return count;
}
static int load_photo(unsigned index, u16 *out)
{
    char name[13]; unsigned n;
    if (index>=count) { return 0; }
    photo_name(name,photo_ids[index]);
    return read_file(name,bitmap,sizeof(bitmap),&n) && bmp_read(bitmap,n,out);
}
static int save_photo(const u16 *pixels)
{
    char name[13];
    unsigned id=1, i, n;
    photo_count();
    if (count==GALLERY_MAX) { return 0; }
    for (i=0; i<count; i++) { if (id<=photo_ids[i]) { id=photo_ids[i]+1; } }
    if (id>99999) { return 0; }
    photo_name(name,id);
    n=bmp_write(pixels,bitmap,sizeof(bitmap));
    return n && fat_create(&fs,name,bitmap,n);
}
static void lens(int selfie)
{
    if (BOARD_ID==2) {
        if (selfie) { REG(0x8010)&=~0x800u; } else { REG(0x8010)|=0x800; }
    }
}
static int unavailable(void) { return 0; }
/* Intentionally no GPIO strobe backend until temperature/supply interlocks
 * and actual head energy limits have been validated on each board. */
static const Platform platform={read_file,save_photo,load_photo,photo_count,
                                0,unavailable,unavailable,BOARD_ID==2 ? lens : 0};
static u8 clip(int v) { return v<0 ? 0 : v>255 ? 255 : v; }
static void sensor_copy(void)
{
    unsigned x,y;
    for (y=0; y<LH; y++) {
        for (x=0; x<LW; x++) {
            int l=sensor_y[y*LW+x];
            int u=sensor_uv[(y/2)*LW+(x&~1u)]-128;
            int v=sensor_uv[(y/2)*LW+(x&~1u)+1]-128;
            int r=clip(l+((359*v)>>8)), g=clip(l-((88*u+183*v)>>8)), b=clip(l+((454*u)>>8));
            camera[y*LW+x]=RGB(r,g,b);
        }
    }
}
static void present(const u16 *f)
{
    unsigned x,y;
    for (y=0; y<320; y++) {
        for (x=0; x<240; x++) {
            u16 c=BOARD_ID==1 ? f[(239-x)*LW+y] : f[x*LW+319-y];
            int r=(c>>11)*255/31, g=((c>>5)&63)*255/63, b=(c&31)*255/31;
            lcd_y[y*240+x]=(77*r+150*g+29*b)>>8;
            if (!(x&1) && !(y&1)) {
                lcd_uv[(y/2)*240+x]=clip(128+((-43*r-85*g+128*b)>>8));
                lcd_uv[(y/2)*240+x+1]=clip(128+((128*r-107*g-21*b)>>8));
            }
        }
    }
    REG(0xc800)=1;
}
static Key key_read(void)
{
    /* Unique ladder levels. X8D has only previous/next, not four directions. */
    static const u16 values[3][9]={
        {657,510,252,776,386,12,1024,1024,1024},
        {411,511,411,511,788,14,252,132,1024},
        {620,775,328,238,512,415,512,133,704}
    };
    static const Key keys[9]={KEY_PREV,KEY_NEXT,KEY_PREV,KEY_NEXT,KEY_OK,KEY_BACK,KEY_OK,KEY_INK,KEY_LENS};
    unsigned i, sample, best=25, at=9, timeout=10000;
    REG(0x9170)=0x10|3|0x80;
    while ((REG(0x9170)&0x40) && --timeout) { }
    if (!timeout) { return KEY_NONE; }
    sample=REG(0x9178)&1023;
    if (sample>990) { return KEY_NONE; }
    for (i=0; i<9; i++) {
        unsigned v=values[BOARD_ID][i];
        unsigned d=sample>v ? sample-v : v-sample;
        if (v<1024 && d<best) { at=i; best=d; }
    }
    return at<9 ? keys[at] : KEY_NONE;
}
static void hardware_init(void)
{
    REG(0x4800)=0;
    REG(0x9800)=1; /* Enable model's periodic clock. */
    REG(0xa800)=1;
    REG(0xc810)=((320-1)<<16)|(240-1);
    REG(0xc818)=1; /* YUV video plane, normal scan. */
    REG(0xc820)=((320-1)<<16)|(240-1);
    REG(0xc824)=0;
    REG(0xc828)=(u32)lcd_y; REG(0xc82c)=240/4;
    REG(0xc830)=(u32)lcd_uv; REG(0xc834)=240/4;
    REG(0xb060)=(u32)sensor_y; REG(0xb064)=(u32)sensor_uv;
    REG(0xb068)=LW; REG(0xb180)=(LH<<16)|LW;
    REG(0xb008)=1u<<19; REG(0xb018)=0x40000; REG(0xb000)=1;
    lens(0);
}
void firmware_main(void)
{
    App *a=(App *)&luma_app;
    Input input={0};
    int have_bg=0, have_boot=0;
    u32 last=~0u, capacity;
    unsigned n;
    hardware_init();
    capacity=sd_hw_init(&sd,dma);
    if (capacity) { fat_mount(&fs,read_sector,write_sector,capacity); }
    app_init(a,&platform,REG(0x9908)/1000);
    if (a->theme.background[0] && read_file(a->theme.background,bitmap,sizeof(bitmap),&n)) { have_bg=bmp_read(bitmap,n,theme_bg); }
    if (a->theme.boot[0] && read_file(a->theme.boot,bitmap,sizeof(bitmap),&n)) { have_boot=bmp_read(bitmap,n,theme_boot); }
    for (;;) {
        u32 now=REG(0x9908)/1000;
        int held;
        Key key=input_poll(&input,key_read(),now,&held);
        if (key!=KEY_NONE) { app_event(a,&platform,key,held,now,camera,photo); }
        if (now-last < 40) { continue; }
        last=now;
        if (a->screen==CAMERA) { sensor_copy(); }
        app_render(a,frame,camera,photo,have_bg ? theme_bg : 0,have_boot ? theme_boot : 0,now);
        present(frame);
        luma_frames++;
    }
}
