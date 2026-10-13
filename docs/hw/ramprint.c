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
        out[7] = runA(canvas, w, 384u, energy, thr, p6);                                /* status: 8 = ok */
        out[8] = t0;
    }
    /* mirror the essentials into the mailbox so a USB timeout cannot lose the result */
    volatile u32 *mb = (volatile u32 *)MAILBOX;
    mb[0] = 0x4D424F58u; mb[1] = go; mb[2] = out[7]; mb[3] = out[0]; mb[4] = out[3];
    CACHE_clean((void *)MAILBOX, 0x40, 0);
    finish(buffer, length, parameter, go ? 3 : 1);
}
