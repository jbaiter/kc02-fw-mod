/* SPDX-License-Identifier: MIT */
#include "luma.h"

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static u16 le16(const u8 *p) { return p[0] | ((u16)p[1] << 8); }
static u32 le32(const u8 *p) { return le16(p) | ((u32)le16(p+2) << 16); }
static void put16(u8 *p, u16 n) { p[0] = n; p[1] = n >> 8; }
static void put32(u8 *p, u32 n) { put16(p, n); put16(p+2, n >> 16); }

void theme_default(Theme *t)
{
    *t = (Theme){RGB(12,18,24), RGB(26,35,44), RGB(241,244,237),
                 RGB(150,167,178), RGB(164,237,191), 1300, 300, "", ""};
}
static int filename(char dest[13], const char *s)
{
    unsigned n = 0, dot = 0, suffix = 0;
    while (*s) {
        unsigned c = (u8)*s++;
        if (n >= 12) { return 0; }
        if (c == '.') { if (dot || !n || n > 8) { return 0; } dot = n; }
        else {
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) { return 0; }
            if (dot) { suffix++; } else if (n >= 8) { return 0; }
        }
        dest[n++] = (char)c;
    }
    dest[n] = 0;
    return dot && suffix == 3 && same(dest + dot, ".BMP");
}
static int hex(const char *s, u16 *color)
{
    unsigned i, rgb = 0;
    if (*s++ != '#') { return 0; }
    for (i = 0; i < 6; i++) {
        unsigned c = (u8)s[i], v;
        if (!c) { return 0; }
        if (c >= '0' && c <= '9') { v = c - '0'; }
        else if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; }
        else if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; }
        else { return 0; }
        rgb = (rgb << 4) | v;
    }
    if (s[6]) { return 0; }
    *color = RGB(rgb >> 16, rgb >> 8, rgb);
    return 1;
}
static int number(const char *s, u32 *n, unsigned lo, unsigned hi)
{
    u32 v = 0;
    if (!*s) { return 0; }
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || v > hi / 10) { return 0; }
        v = v * 10 + *s - '0';
        if (v > hi) { return 0; }
    }
    if (v < lo) { return 0; }
    *n = v;
    return 1;
}
int theme_parse(Theme *out, const u8 *bytes, unsigned size)
{
    Theme t;
    unsigned pos = 0;
    theme_default(&t);
    if (size > THEME_MAX) { return 0; }
    while (pos < size) {
        char line[128], *key, *value, *end;
        unsigned n = 0;
        while (pos < size && bytes[pos] != '\n') {
            unsigned c = bytes[pos++];
            if (n == sizeof(line)-1 || (!c) || (c < 32 && c != '\r' && c != '\t') || c > 126) { return 0; }
            line[n++] = c;
        }
        if (pos < size) { pos++; }
        line[n] = 0;
        key = line;
        while (*key == ' ' || *key == '\t') { key++; }
        if (!*key || *key == ';' || *key == '#') { continue; }
        value = key;
        while (*value && *value != '=') { value++; }
        if (!*value) { return 0; }
        end = value;
        while (end > key && (end[-1] == ' ' || end[-1] == '\t')) { end--; }
        *end = 0;
        value++;
        while (*value == ' ' || *value == '\t') { value++; }
        end = value;
        while (*end) { end++; }
        while (end > value && (end[-1] == ' ' || end[-1] == '\r' || end[-1] == '\t')) { end--; }
        *end = 0;
        if (same(key,"background")) { if (!hex(value,&t.bg)) { return 0; } }
        else if (same(key,"panel")) { if (!hex(value,&t.panel)) { return 0; } }
        else if (same(key,"text")) { if (!hex(value,&t.text)) { return 0; } }
        else if (same(key,"muted")) { if (!hex(value,&t.muted)) { return 0; } }
        else if (same(key,"accent")) { if (!hex(value,&t.accent)) { return 0; } }
        else if (same(key,"boot_ms")) { if (!number(value,&t.boot_ms,300,3000)) { return 0; } }
        else if (same(key,"shutter_ms")) { if (!number(value,&t.shutter_ms,100,600)) { return 0; } }
        else if (same(key,"boot_image")) { if (!filename(t.boot,value)) { return 0; } }
        else if (same(key,"background_image")) { if (!filename(t.background,value)) { return 0; } }
        else { return 0; }
    }
    *out = t; /* Invalid input never partially mutates the active theme. */
    return 1;
}
int bmp_read(const u8 *b, unsigned size, u16 *out)
{
    unsigned x, y, start;
    i32 height;
    if (size < 54 || b[0] != 'B' || b[1] != 'M' || le32(b+14) != 40 ||
        le32(b+18) != LW || le16(b+26) != 1 || le16(b+28) != 24 || le32(b+30)) { return 0; }
    height = (i32)le32(b+22);
    start = le32(b+10);
    if ((height != LH && height != -LH) || start < 54 || start > size ||
        LW*LH*3u > size-start) { return 0; }
    for (y = 0; y < LH; y++) {
        const u8 *row = b + start + (height > 0 ? LH-1-y : y)*LW*3;
        for (x = 0; x < LW; x++) { out[y*LW+x] = RGB(row[x*3+2],row[x*3+1],row[x*3]); }
    }
    return 1;
}
unsigned bmp_write(const u16 *pixels, u8 *out, unsigned cap)
{
    unsigned i, x, y, size = 54 + LW*LH*3;
    if (cap < size) { return 0; }
    for (i = 0; i < 54; i++) { out[i] = 0; }
    out[0] = 'B'; out[1] = 'M'; put32(out+2,size); put32(out+10,54);
    put32(out+14,40); put32(out+18,LW); put32(out+22,LH);
    put16(out+26,1); put16(out+28,24); put32(out+34,LW*LH*3);
    for (y = 0; y < LH; y++) {
        u8 *row = out + 54 + (LH-1-y)*LW*3;
        for (x = 0; x < LW; x++) {
            u16 c = pixels[y*LW+x];
            row[x*3] = (c&31)*255/31;
            row[x*3+1] = ((c>>5)&63)*255/63;
            row[x*3+2] = (c>>11)*255/31;
        }
    }
    return size;
}
void print_row(const u16 *image, unsigned y, Ink ink, u8 dots[384])
{
    static const u8 bayer[16] = {0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    unsigned x;
    if (y >= 288 || ink < INK_OFF || ink > INK_GRAY) {
        for (x = 0; x < 384; x++) { dots[x] = 0; }
        return;
    }
    for (x = 0; x < 384; x++) {
        unsigned c = image[(y*LH/288)*LW + x*LW/384];
        unsigned luma = (((c>>11)*255/31)*77 + (((c>>5)&63)*255/63)*150 + ((c&31)*255/31)*29) >> 8;
        unsigned darkness = 255-luma;
        dots[x] = ink == INK_OFF ? 0 : ink == INK_GRAY ? (darkness*31+127)/255 :
            (darkness > (unsigned)bayer[(y&3)*4+(x&3)]*16+7 ? 31 : 0);
    }
}
u32 crc32(const u8 *p, unsigned size)
{
    u32 crc = ~0u;
    unsigned i;
    while (size--) {
        crc ^= *p++;
        for (i = 0; i < 8; i++) { crc = (crc >> 1) ^ (0xedb88320u & (0u-(crc&1))); }
    }
    return ~crc;
}
