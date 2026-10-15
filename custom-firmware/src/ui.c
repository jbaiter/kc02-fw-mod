/* SPDX-License-Identifier: MIT */
#include "luma.h"
#include "paint.h"
#include "font.inc"

const char *const ink_names[3] = {"No print", "Dither", "Grayscale"};
static void copy_text(char *out, const char *s, unsigned n)
{
    unsigned i = 0;
    while (s[i] && i+1 < n) { out[i] = s[i]; i++; }
    out[i] = 0;
}
void app_notice(App *a, const char *s, u32 now)
{
    copy_text(a->notice,s,sizeof(a->notice));
    a->notice_until = now + 2200;
}
static void ink_next(App *a, int direction)
{
    a->ink = (Ink)((a->ink + (direction > 0 ? 1 : 2)) % 3);
}
static int gallery_load(App *a, const Platform *p, u16 *photo)
{
    unsigned i;
    /* A damaged/missing photo must not silently display the preceding one. */
    for (i=0; i<PIXELS; i++) { photo[i]=a->theme.bg; }
    return p->load_photo && p->load_photo(a->photo,photo);
}
void app_init(App *a, const Platform *p, u32 now)
{
    u8 config[THEME_MAX];
    unsigned n;
    *a = (App){0};
    theme_default(&a->theme);
    a->ink = INK_OFF; /* Never expend paper at first boot. */
    a->boot_at = now;
    a->booting = 1;
    if (p->read_file && p->read_file("THEME.INI",config,sizeof(config),&n)) {
        if (!theme_parse(&a->theme,config,n)) { app_notice(a,"Invalid theme - using defaults",now+1800); }
    }
    a->count = p->photo_count ? p->photo_count() : 0;
}
void app_event(App *a, const Platform *p, Key key, int held, u32 now,
               const u16 *camera, u16 *photo)
{
    unsigned i;
    if (a->booting || a->shutter) { return; }
    if (key == KEY_BACK) {
        a->screen = (Screen)((a->screen+1)%3);
        a->setting = 0;
        a->notice[0] = 0;
        if (a->screen == GALLERY && a->count && !gallery_load(a,p,photo)) {
            app_notice(a,"Photo unavailable",now);
        }
        return;
    }
    if (key == KEY_INK || (key == KEY_OK && held && a->screen != SETTINGS)) { ink_next(a,1); return; }
    if (held) { return; }
    if (key == KEY_LENS && a->screen == CAMERA && p->lens) {
        a->lens = !a->lens; p->lens(a->lens); return;
    }
    if (a->screen == SETTINGS) {
        if (key == KEY_PREV) { a->setting = (a->setting+3)%4; }
        if (key == KEY_NEXT) { a->setting = (a->setting+1)%4; }
        if (key == KEY_OK) {
            switch (a->setting) {
            case 0: ink_next(a,1); break;
            case 1: app_notice(a,p->usb_storage && p->usb_storage() ? "SD exported over USB" : "USB storage not ready",now); break;
            case 2: app_notice(a,p->update_check && p->update_check() ? "Update ready for confirmation" : "Updater not ready",now); break;
            default: app_notice(a,"Luma 0.1 - development build",now); break;
            }
        }
        return;
    }
    if (a->screen == CAMERA) {
        if (key == KEY_PREV) { ink_next(a,-1); }
        if (key == KEY_NEXT) { ink_next(a,1); }
        if (key == KEY_OK) {
            for (i = 0; i < PIXELS; i++) { photo[i] = camera[i]; }
            a->shutter = 1; a->shutter_at = now;
            if (p->save_photo && p->save_photo(photo)) {
                a->count = p->photo_count();
                a->photo = a->count ? a->count-1 : 0;
                app_notice(a,"Photo saved",now);
                if (a->ink != INK_OFF && (!p->print_photo || !p->print_photo(photo,a->ink))) {
                    app_notice(a,"Saved - printer not ready",now);
                }
            } else { app_notice(a,"Not saved - check SD card",now); }
        }
    } else {
        if ((key == KEY_PREV || key == KEY_NEXT) && a->count) {
            a->photo = (a->photo + (key == KEY_NEXT ? 1 : a->count-1)) % a->count;
            if (!gallery_load(a,p,photo)) { app_notice(a,"Photo unavailable",now); }
        }
        if (key == KEY_OK && a->count) {
            if (a->ink == INK_OFF) { app_notice(a,"No print - hold OK to change",now); }
            else { app_notice(a,p->print_photo && p->print_photo(photo,a->ink) ? "Printing" : "Printer not ready",now); }
        }
    }
}

static void text(u16 *f, int x, int y, const char *s, u16 color, int large)
{
    const Glyph *glyphs = large ? large_glyphs : small_glyphs;
    const u8 *pixels = large ? large_pixels : small_pixels;
    unsigned h = large ? 24 : 15;
    while (*s) {
        unsigned c = (u8)*s++, xx, yy;
        const Glyph *g;
        if (c < 32 || c > 126) { c = '?'; }
        g = glyphs + c-32;
        for (yy = 0; yy < h; yy++) {
            for (xx = 0; xx < g->w; xx++) {
                int px = x+(int)xx, py = y+(int)yy;
                if (px >= 0 && px < LW && py >= 0 && py < LH) {
                    unsigned a = pixels[g->off+yy*g->w+xx];
                    if (a) { f[py*LW+px] = blend(f[py*LW+px],color,a); }
                }
            }
        }
        x += g->advance;
    }
}
static void digits(char *out, unsigned n)
{
    out[0] = '0' + n/10%10; out[1] = '0'+n%10; out[2] = 0;
}
void app_render(App *a, u16 *f, const u16 *camera, const u16 *photo,
                const u16 *background, const u16 *boot, u32 now)
{
    Theme *t = &a->theme;
    unsigned i, row;
    u32 elapsed = now-a->boot_at;
    const char *const titles[] = {"Camera","Gallery","Settings"};
    if (a->booting && elapsed >= t->boot_ms) { a->booting = 0; }
    if (a->shutter && now-a->shutter_at >= t->shutter_ms) { a->shutter = 0; }
    for (i = 0; i < PIXELS; i++) { f[i] = t->bg; }
    if (a->booting) {
        unsigned progress = elapsed*255/t->boot_ms;
        int radius8 = 18*8+(int)(progress*progress/512);
        if (boot) { for (i = 0; i < PIXELS; i++) { f[i] = blend(t->bg,boot[i],progress); } }
        ring8(f,160,87,radius8+7*8,8,t->panel);
        ring8(f,160,87,radius8,16,t->accent);
        ring(f,160,87,9,2,t->text);
        box(f,167,67,3,3,t->accent);
        text(f,136,132,"Luma",t->text,1);
        text(f,111,164,"Keep the good bits.",t->muted,0);
        round_box(f,120,201,80,2,0,t->panel);
        box(f,120,201,(int)(progress*80/255),2,t->accent);
        return;
    }
    if (a->screen == CAMERA || (a->screen == GALLERY && a->count)) {
        const u16 *src = a->screen == CAMERA ? camera : photo;
        /* Letterboxed photograph: preserve 4:3, never stretch into HUD. */
        for (row = 0; row < 180; row++) {
            unsigned x;
            for (x = 0; x < 240; x++) { f[(row+36)*LW+x+40] = src[(row*LH/180)*LW+x*LW/240]; }
        }
        if (a->screen == CAMERA) {
            for (i = 0; i < 4; i++) {
                int x = i&1 ? 267 : 46, y = i&2 ? 201 : 42;
                box(f,x,y,7,1,t->text); box(f,i&1 ? x+6 : x,y,1,7,t->text);
            }
        }
    } else if (a->screen == SETTINGS) {
        const char *const items[] = {"Print mode","USB storage","SD firmware update","About Luma"};
        if (background) { for (i = 0; i < PIXELS; i++) { f[i] = background[i]; } }
        for (i = 0; i < 4; i++) {
            int y = 52+i*36;
            int selected = a->setting == i;
            if (selected) { round_box(f,14,y,292,33,7,t->panel); box(f,14,y+10,2,13,t->accent); }
            text(f,27,y+7,items[i],selected ? t->text : t->muted,0);
            text(f,i == 0 ? 224 : 280,y+7,i == 0 ? ink_names[a->ink] : ">",t->accent,0);
        }
    } else {
        ring(f,160,105,20,1,t->panel);
        box(f,148,98,24,17,t->muted); box(f,151,95,7,3,t->muted);
        ring(f,160,106,5,2,t->bg);
        text(f,113,143,"Nothing here yet",t->text,0);
        text(f,85,164,"Your next favourite is out there.",t->muted,0);
    }
    /* Persistent navigation rail. Only three application screens. */
    box(f,0,0,LW,34,t->bg);
    text(f,15,8,"L",t->accent,1);
    for (i = 0; i < 3; i++) {
        int x = 45+i*89;
        text(f,x,11,titles[i],a->screen == (Screen)i ? t->text : t->muted,0);
        if (a->screen == (Screen)i) { round_box(f,x,30,24,2,0,t->accent); }
    }
    if (a->screen != SETTINGS) {
        char n[3];
        round_box(f,220,42,84,22,6,t->bg);
        text(f,230,46,ink_names[a->ink],t->accent,0);
        if (a->screen == GALLERY && a->count) {
            digits(n,a->photo+1); text(f,17,45,n,t->text,0);
            text(f,35,45,"/",t->muted,0); digits(n,a->count); text(f,43,45,n,t->muted,0);
        }
    }
    box(f,0,219,LW,21,t->bg);
    text(f,14,222,a->screen == SETTINGS ? "ARROWS: select   OK: change" : "HOLD OK: ink   BACK: screens",t->muted,0);
    if (a->notice[0] && (i32)(a->notice_until-now) > 0) {
        round_box(f,19,181,282,30,7,t->panel); text(f,30,188,a->notice,t->text,0);
    }
    if (a->shutter) {
        unsigned phase = (now-a->shutter_at)*512/t->shutter_ms;
        unsigned cover = phase < 256 ? phase : 512-phase;
        int h = cover*92/256;
        box(f,40,36,240,h,t->bg); box(f,40,216-h,240,h,t->bg);
        if (phase > 230 && phase < 280) { box(f,40,36,240,180,t->text); }
    }
}
