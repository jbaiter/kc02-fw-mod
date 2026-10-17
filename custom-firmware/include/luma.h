/* SPDX-License-Identifier: MIT
 * Luma: shared, allocation-free camera UI. No stock firmware entry points. */
#ifndef LUMA_H
#define LUMA_H
#include <stddef.h>
/* These exact-width types also work with the freestanding GCC bootstrap. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int i32;
#define LW 320
#define LH 240
#define PIXELS (LW * LH)
/* Valid live-image rows at the start of the RGB camera buffer: 320x180, 16:9.
 * The buffer remains PIXELS long for the existing snapshot/storage ABI. */
#define CAMERA_ROWS 180
#define GALLERY_MAX 64
#define THEME_MAX 4096
#define RGB(r,g,b) ((u16)((((r)&248)<<8)|(((g)&252)<<3)|(((b)&255)>>3)))

typedef enum { CAMERA, GALLERY, SETTINGS } Screen;
typedef enum { INK_OFF, INK_DITHER, INK_GRAY } Ink;
typedef enum { KEY_NONE, KEY_PREV, KEY_NEXT, KEY_OK, KEY_BACK, KEY_INK, KEY_LENS } Key;
typedef struct {
    u16 bg, panel, text, muted, accent;
    u32 boot_ms, shutter_ms;
    char boot[13], background[13];
} Theme;
typedef struct {
    Screen screen;
    Ink ink;
    unsigned setting, photo, count;
    u32 boot_at, shutter_at, notice_until;
    int booting, shutter, lens;
    char notice[40];
    Theme theme;
    unsigned photo_rows; /* 180 for new 16:9 captures, 240 for legacy photos */
} App;

enum { PHOTO_SAVED=1, PHOTO_QUEUED=2, PHOTO_BUSY=3, PHOTO_FULL=4 };

/* Board operations fail explicitly. A return value of 0 means unavailable. */
typedef struct {
    int (*read_file)(const char *name, u8 *out, unsigned cap, unsigned *size);
    int (*save_photo)(const u16 *pixels);
    int (*load_photo)(unsigned index, u16 *pixels);
    unsigned (*photo_count)(void);
    int (*print_photo)(const u16 *pixels, Ink mode);
    int (*usb_storage)(void);
    int (*update_check)(void);
    void (*lens)(int selfie);
} Platform;

extern const char *const ink_names[3];
void theme_default(Theme *t);
int theme_parse(Theme *out, const u8 *bytes, unsigned size);
int bmp_read(const u8 *bytes, unsigned size, u16 *out);
/* Photos accept 320x180 or legacy 320x240; returns valid row count or zero.
 * The RGB allocation stays PIXELS long; rows beyond the image become black. */
unsigned photo_bmp_read(const u8 *bytes, unsigned size, u16 *out);
unsigned photo_bmp_write(const u16 *pixels, u8 *out, unsigned cap);
/* Validates a BMP header and declared length without decoding any pixels. */
int bmp_check(const u8 *bytes, unsigned size);
unsigned bmp_write(const u16 *pixels, u8 *out, unsigned cap);
void app_init(App *a, const Platform *p, u32 now);
void app_event(App *a, const Platform *p, Key key, int held, u32 now,
               const u16 *camera, u16 *photo);
void ui_text(u16 *f, int x, int y, const char *s, u16 color, int large);
void app_render(App *a, u16 *frame, const u16 *camera, const u16 *photo,
                const u16 *background, const u16 *boot, u32 now);
void app_notice(App *a, const char *text, u32 now);
/* 384-dot grayscale energy levels 0..31. Does not energise GPIO. */
void print_row(const u16 *image, unsigned y, Ink ink, u8 dots[384]);
u32 crc32(const u8 *data, unsigned size);
#endif
