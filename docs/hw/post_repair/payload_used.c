/* Print test directly from RAM - no card, no gallery, no JPEG, no mode change.
   Canvas: luma, row-major, canvas[head_pos*w + line]; 0x00 = ink, 0xFF = white.
     head_pos = 0..383 (head dots across the paper), line = 0..w-1 (paper feed).
   Job wrapper kc02_print_run_A(canvas,w,384,energy,threshold_mode,param6) does the pin setup,
   head power, strobe ramp, paper sensor and the 40-step feed itself.
   INPUT[0]=w (feed lines, 8..256) INPUT[1]=energy 0..3 (higher=darker) INPUT[2]=threshold mode
   INPUT[3]=0 fill only / 1 print        INPUT[4]=param6 (0 = take the live app_context byte 0)
   Pattern: 1-px ink line at head_pos 0 + control at 16 (dropped-column test), three full-width
   bands (across-paper width test), and an 8-line-pitch ruler in the feed direction. */
#include "common.h"
typedef void *(*alloc_t)(u32 size, u32 align);
typedef void (*fn0_t)(void);
typedef int  (*run_a_t)(void *canvas, u32 w, u32 h, u32 energy, u32 thr, u32 p6);
#define MALLOC_FN  0x0203E5C4u
#define PRN_INIT   0x02059F84u
#define RUN_A      0x0205A6C4u
#define APP_CTX    0x020D416Cu
#define MAILBOX    0x020DB300u

/* 7-segment predicate: returns 1 if canvas pixel (rx,ry) of digit d is ink.
   Used inside the fill loop so the digits survive the single canvas write pass. */
static const uint8_t SEG7[10] = { 0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F };
static uint32_t seg7_px(uint32_t d, uint32_t rx, uint32_t ry, uint32_t wd, uint32_t ht, uint32_t t)
{
    uint8_t m = SEG7[d % 10u];
    if ((m & 0x01u) && ry < t) return 1;
    if ((m & 0x02u) && rx >= wd - t && ry < ht / 2u) return 1;
    if ((m & 0x04u) && rx >= wd - t && ry >= ht / 2u) return 1;
    if ((m & 0x08u) && ry >= ht - t) return 1;
    if ((m & 0x10u) && rx < t && ry >= ht / 2u) return 1;
    if ((m & 0x20u) && rx < t && ry < ht / 2u) return 1;
    if ((m & 0x40u) && ry + t / 2u >= ht / 2u && ry < ht / 2u + t / 2u + 1u) return 1;
    return 0;
}
void test_entry(void *buffer, u32 length, u32 parameter)

{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    alloc_t sysMalloc = (alloc_t)MALLOC_FN;
    fn0_t   prnInit   = (fn0_t)PRN_INIT;
    run_a_t runA      = (run_a_t)RUN_A;
    u32 w = in[0], energy = in[1], thr = in[2], go = in[3], p6 = in[4], pat = in[5];
    if (w < 8) w = 48; if (w > 256) w = 256;
    uint8_t *canvas = (uint8_t *)sysMalloc(w * 384u, 0x20u);
    out[0] = (u32)canvas; out[1] = w;
    if (canvas == (uint8_t *)0x0) { out[2] = 0xE1u; finish(buffer, length, parameter, 2); return; }
    u32 ink_px = 0;
    for (u32 hp = 0; hp < 384u; ++hp) {
        for (u32 col = 0; col < w; ++col) {
            u32 ink = 0;
            if (pat == 1u) {
                /* LOW-CURRENT COUNTING PATTERN (a few dozen dots per line only):
                   - dashes at head dots 0,8,16,...,376 and one at 383, 8 feed lines long  -> count them
                     (49 dashes if column 0 is driven, 48 if it is dropped)
                   - a second comb offset by 4 dots (dot 4,12,...,380) 8 lines long 16 lines later -> pitch check
                   - four 1-dot lines at head dots 0,1,2,3, 64 feed lines long -> count 4 vs 3
                   - two 1-dot edge stubs at dots 0 and 383, 64 lines long -> count 2 vs 1 */
                if (col < 8u && ((hp & 7u) == 0u || hp == 383u)) ink = 1;   /* 49 dashes -> count 49 vs 48 */
                if (col >= 16u && col < 24u && (hp & 7u) == 4u) ink = 1;     /* offset comb -> pitch check */
                /* four 1-dot lines at head dots 0,4,8,12 (0.5 mm apart, countable) -> 4 vs 3 */
                if ((hp == 0u || hp == 4u || hp == 8u || hp == 12u) && col >= 32u) ink = 1;
                if (hp == 383u && col >= 32u) ink = 1;                       /* right-edge 1-dot line */
            } else if (pat == 2u) {
                /* TEST CARD: "3292" digits + a hatched density ladder + corner ticks (sparse) */
                u32 x = hp, y = col;
                if ((x == 0u || x == 383u) && y < 8u) ink = 1;                       /* corner ticks */
                if ((y == 0u || y == 7u) && (x % 64u) == 0u) ink = 1;
                for (u32 k = 0; k < 4u; ++k) {                                        /* "3292" digits */
                    u32 d = (k == 0u) ? 3u : (k == 1u) ? 2u : (k == 2u) ? 9u : 2u;
                    u32 bx = 132u + k * 32u;
                    if (hp >= bx && hp < bx + 24u && col >= 76u && col < 120u &&
                        seg7_px(d, hp - bx, col - 76u, 24u, 44u, 6u)) ink = 1;
                }
                for (u32 k = 0; k < 8u; ++k) {                                        /* density ladder */
                    u32 bx = 32u + k * 44u;
                    if (x >= bx && x < bx + 16u && y >= 40u && y < 56u) {
                        u32 step = k + 1u;                                            /* 1..8 of 8 -> 12..100% */
                        if (((x - bx) % 8u) < step && ((y - 40u) % 2u) == 0u) ink = 1;
                    }
                }
            } else if (pat == 5u) {
                /* DOT-COUNT STAIRCASE: feed lines in groups of 8 (1 mm); group k sets k*4+4 dots.
                   The highest group still visible on paper = max transferable dots per line. */
                u32 n = 4u + 4u * (col / 8u);
                if (hp < n) ink = 1;
            } else if (pat == 6u) {
                /* AXIS SEPARATION.
                   (a) feed lines 0..15: exactly 4 dots at each of 8 head positions
                       -> tests POSITION only (count fixed)
                   (b) feed lines 24..87: n dots left-aligned, n = 2,4,8,...,256
                       -> tests COUNT only (all in the left region) */
                if (col < 16u) {
                    u32 idx = col / 2u;
                    u32 p = (idx == 0u) ? 4u : (idx == 1u) ? 64u : (idx == 2u) ? 128u :
                            (idx == 3u) ? 192u : (idx == 4u) ? 256u : (idx == 5u) ? 320u :
                            (idx == 6u) ? 352u : 376u;
                    if (hp >= p && hp < p + 4u) ink = 1;
                }
                if (col >= 24u && col < 88u) {
                    u32 g = (col - 24u) / 8u;
                    u32 n = 2u << g;
                    if (hp < n) ink = 1;
                }
            } else if (pat == 7u) {
                /* DENSITY vs POSITION, with the count held equal:
                   (a) feed lines 0..15  : eight 4-dot marks at UNIFORM 48-dot spacing (pitch check)
                   (b) feed lines 24..39 : 24-dot bar left-aligned  and 24-dot bar centred, 4 lines each
                   (c) feed lines 48..63 : 48-dot bar left-aligned  and 48-dot bar centred, 4 lines each
                   Left bar printing while the centred twin does not == the head's rail feeds from the
                   left end and sags along its length (a positional effect, not a count limit). */
                if (col < 16u && ((col / 2u) % 2u) == 0u) {
                    u32 k = (col / 2u) / 2u;                       /* 0..3 -> two rows per mark set */
                    if (k < 8u) { u32 px = 8u + 48u * k; if (hp >= px && hp < px + 4u) ink = 1; }
                }
                if (col >= 24u && col < 32u) { if (hp < 24u) ink = 1; }             /* left  24 */
                if (col >= 32u && col < 40u) { if (hp >= 180u && hp < 204u) ink = 1; }/* centre 24 */
                if (col >= 48u && col < 56u) { if (hp < 48u) ink = 1; }             /* left  48 */
                if (col >= 56u && col < 64u) { if (hp >= 168u && hp < 216u) ink = 1; }/* centre 48 */
            } else if (pat == 3u) {
                /* MEASUREMENT RULER: feed ticks at both edges (1 mm) + a 48-mark head comb */
                u32 x = hp, y = col;
                if ((y % 8u) == 0u) {                                                 /* 1 mm feed ticks */
                    u32 ext = ((y % 64u) == 0u) ? 16u : 8u;                           /* 5 mm marks */
                    if (x < ext || x >= 384u - ext) ink = 1;
                }
                if (y >= 32u && y < 40u && (x % 8u) == 0u) ink = 1;                   /* head-axis comb */
            } else {
                if ((hp == 0u || hp == 16u) && col >= 4u && col < w - 4u) ink = 1;      /* edge + control */
                if ((col >= 8u && col < 14u) || (col >= 24u && col < 30u) ||
                    (col >= 40u && col < 46u)) ink = 1;                                /* full-width bands */
                if (hp >= 32u && hp < 96u && (col & 7u) == 0u) ink = 1;                /* feed ruler */
            }
            if (ink) { ink_px++; canvas[hp * w + col] = (uint8_t)0x00u; } else { canvas[hp * w + col] = (uint8_t)0xFFu; }
        }
    }
    out[3] = ink_px;
    out[4] = *(volatile uint8_t *)(APP_CTX + 0x60u);
    out[5] = *(volatile uint8_t *)(APP_CTX + 0x61u);
    out[6] = *(volatile uint8_t *)(APP_CTX);
    if (go) {
        if (p6 == 0u) p6 = *(volatile uint8_t *)(APP_CTX);
        prnInit();
        u32 t0 = 0;
        out[7] = runA(canvas, w, 384u, energy, thr, p6);
        out[9] = ((int (*)(void))0x02059290u)();   /* status code the strobe bias is derived from */                                /* status: 8 = ok */
        out[8] = t0;
    }
    /* mirror the essentials into the mailbox so a USB timeout cannot lose the result */
    volatile u32 *mb = (volatile u32 *)MAILBOX;
    mb[0] = 0x4D424F58u; mb[1] = go; mb[2] = out[7]; mb[3] = out[0]; mb[4] = out[3];
    CACHE_clean((void *)MAILBOX, 0x40, 0);
    finish(buffer, length, parameter, go ? 3 : 1);
}
