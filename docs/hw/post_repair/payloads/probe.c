/* Small diagnostic payload: a read-only printer/paper/battery report, and optionally a short
   test strip that exercises the feed mechanism.

   INPUT[0] = 0 report only, 1 report + short test print
   INPUT[1] = test-strip feed lines (8..256, default 64)
   INPUT[2] = energy
   INPUT[3] = threshold mode

   Everything read here is a pure read: kc02_sensor_read is hal_gpioRead(0, 0x80), the battery
   calls only sample the ADC, and the rest are RAM reads. No head drive unless INPUT[0] is 1.
*/
#ifndef TEST_ID
#define TEST_ID 0x0FBAD100
#endif
#include "common.h"
#include "art_font.inc"

typedef int (*int0_t)(void);
typedef u32 (*u320_t)(void);
typedef u32 (*gpio_read_t)(u32 group, u32 mask);
typedef void *(*alloc_t)(u32 size, u32 align);
typedef void (*free_t)(void *buffer);
typedef void (*fn0_t)(void);
typedef int (*run_a_t)(void *canvas, u32 w, u32 h, u32 energy, u32 thr, u32 p6);

#define SENSOR_READ 0x020593A4u      /* 0 = paper/cover sensor says paper is present */
#define BATT_CHECK 0x02059290u
#define GET_ADC 0x020035B8u
#define BATT_READ 0x020034C4u
#define GPIO_READ 0x020386B8u
#define MALLOC_FN 0x0203E5C4u
#define FREE_FN 0x0203E694u
#define PRN_INIT 0x02059F84u
#define RUN_A 0x0205A6C4u
#define APP_CTX 0x020D416Cu
#define PRN_STATE 0x020D2EC8u
#define SENSOR_CACHE 0x020E3220u
#define MAILBOX 0x020DB300u
#define MAILBOX_MAGIC 0x4D424F58u
#define HEAD_DOTS 384u

static uint8_t *canvas;
static u32 canvas_w;

static u32 phys_x(u32 x)
{
    return HEAD_DOTS - 1u - x;      /* head dot 0 is the paper's right edge */
}
static void put(u32 x, u32 y)
{
    if (x < HEAD_DOTS && y < canvas_w) {
        canvas[phys_x(x) * canvas_w + y] = 0x00u;
    }
}
static void box(u32 x0, u32 y0, u32 x1, u32 y1)
{
    for (u32 y = y0; y <= y1; ++y) {
        for (u32 x = x0; x <= x1; ++x) {
            put(x, y);
        }
    }
}
static void glyph(u32 x, u32 y, char character, u32 scale)
{
    const unsigned char *rows = (const unsigned char *)0;
    for (u32 index = 0; index < sizeof(ART_FONT) / sizeof(ART_FONT[0]); ++index) {
        if ((char)ART_FONT[index][0] == character) {
            rows = ART_FONT[index] + 1;
            break;
        }
    }
    if (rows == (const unsigned char *)0) {
        return;
    }
    for (u32 row = 0; row < ART_FONT_ROWS; ++row) {
        unsigned char bits = rows[row];
        for (u32 column = 0; column < 5u; ++column) {
            if ((bits >> (4u - column)) & 1u) {
                box(x + column * scale, y + row * scale,
                    x + column * scale + scale - 1u, y + row * scale + scale - 1u);
            }
        }
    }
}
static void text(u32 x, u32 y, const char *message, u32 scale)
{
    u32 cursor = x;
    for (const char *walk = message; *walk != '\0'; ++walk) {
        glyph(cursor, y, *walk, scale);
        cursor += 6u * scale;
    }
}
static void text_u32(u32 x, u32 y, u32 value, u32 scale)
{
    char digits[11];
    u32 count = 0u;
    if (value == 0u) {
        digits[count++] = '0';
    }
    while (value != 0u) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    u32 cursor = x;
    while (count != 0u) {
        glyph(cursor, y, digits[--count], scale);
        cursor += 6u * scale;
    }
}

/* Short strip: a left-edge marker, the word KC02, one-millimetre feed ticks on both edges,
   and a full-width tail bar. Enough to confirm orientation and a clean, even feed. */
static void render_test(u32 lines)
{
    box(0u, 3u, 17u, 22u);                       /* solid marker: reader's left edge */
    text(30u, 4u, "KC02", 2u);
    text(30u, 24u, "TEST", 1u);
    text(30u, 36u, "LINES", 1u);
    text_u32(72u, 36u, lines, 1u);
    for (u32 y = 0; y < lines; y += 8u) {        /* 1 mm ticks */
        box(0u, y, 9u, y);
        box(HEAD_DOTS - 10u, y, HEAD_DOTS - 1u, y);
    }
    for (u32 x = 0; x < HEAD_DOTS; x += 32u) {   /* across-paper reference comb */
        box(x, 52u, x, 59u);
    }
    if (lines > 6u) {
        box(0u, lines - 6u, HEAD_DOTS - 1u, lines - 1u);
    }
}

__attribute__((section(".entry"), used, noinline))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    volatile u32 *mb = (volatile u32 *)MAILBOX;
    volatile u32 *ps = (volatile u32 *)PRN_STATE;
    int0_t sensorRead = (int0_t)SENSOR_READ;
    int0_t battCheck = (int0_t)BATT_CHECK;
    u320_t getAdc = (u320_t)GET_ADC;
    int0_t battRead = (int0_t)BATT_READ;
    gpio_read_t gpioRead = (gpio_read_t)GPIO_READ;
    u32 do_print = in[0], lines = in[1], energy = in[2], threshold = in[3];

    out[0] = (u32)sensorRead();                      /* 0 = paper present, else no paper/lid */
    out[1] = (u32)battCheck();
    out[2] = getAdc();
    out[3] = (u32)battRead();
    out[4] = gpioRead(0u, 0x80u);                    /* same read the sensor call performs */
    for (u32 index = 0; index < 8u; ++index) {
        out[5 + index] = ps[index];
    }
    out[13] = *(volatile unsigned char *)(APP_CTX);
    out[14] = *(volatile unsigned char *)(APP_CTX + 0x60u);
    out[15] = *(volatile unsigned char *)(APP_CTX + 0x61u);
    out[16] = *(volatile unsigned char *)(SENSOR_CACHE);

    if (lines < 8u) { lines = 64u; }
    if (lines > 256u) { lines = 256u; }
    if (do_print) {
        canvas = (uint8_t *)((alloc_t)MALLOC_FN)(lines * HEAD_DOTS, 0x20u);
        canvas_w = lines;
        out[17] = (u32)canvas;
        if (canvas != (uint8_t *)0x0) {
            for (u32 index = 0; index < lines * HEAD_DOTS; ++index) {
                canvas[index] = 0xFFu;
            }
            render_test(lines);
            u32 ink = 0u, hash = 2166136261u;
            for (u32 index = 0; index < lines * HEAD_DOTS; ++index) {
                u32 value = canvas[index];
                if (value == 0u) { ++ink; }
                hash = (hash ^ value) * 16777619u;
            }
            out[19] = ink;
            out[20] = hash;
            if (threshold == 0u) { threshold = 1u; }   /* keep the strip pure black/white */
            ((fn0_t)PRN_INIT)();
            out[18] = (u32)((run_a_t)RUN_A)(canvas, lines, HEAD_DOTS, energy, threshold,
                                            *(volatile unsigned char *)(APP_CTX));
            ((free_t)FREE_FN)(canvas);
            canvas = (uint8_t *)0x0;
        }
    }
    mb[0] = MAILBOX_MAGIC; mb[1] = out[0]; mb[2] = out[1]; mb[3] = out[18]; mb[4] = do_print;
    CACHE_clean((void *)MAILBOX, 0x40, 0);
    finish(buffer, length, parameter, 5);
}
