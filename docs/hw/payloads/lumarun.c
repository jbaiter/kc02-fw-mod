/* Bounded hardware RAM-run driver for Luma's bench entry.
 *
 * INPUT[0] = Luma bench entry address
 * INPUT[1] = Luma beacon address
 * INPUT[2] = Luma __bss_start
 * INPUT[3] = Luma __bss_end
 * INPUT[4] = number of display commits to wait for before restoring, 0..64
 *
 * The hold exists because DE commits are frame-gated: Luma's present() and the
 * restore below both happen inside one frame, so without waiting the panel never
 * scans out Luma's buffer and nothing appears. It waits by polling DE_CTRL bit 1,
 * which hardware clears at the frame boundary that completes a commit, so one
 * unit is exactly one realised frame. An earlier version used a millisecond
 * sleep; a long delay starved the stock USB stack and wedged the device, whereas
 * a bounded register poll cannot overshoot.
 *
 * The host has already uploaded the bench image to its linked base and taken a
 * snapshot of the whole window. This payload then:
 *   1. saves the display registers Luma is about to change,
 *   2. zeroes Luma's .bss so its statics start from a known state,
 *   3. calls the real Luma entry, which runs hardware_init + app_render +
 *      present for exactly one frame and then RETURNS,
 *   4. copies the beacon out, and
 *   5. puts every display register back, so the panel returns to the stock
 *      image instead of scanning out a buffer that is about to be restored.
 *
 * INPUT[0] = 0 is refused: this payload must not run without a real target.
 * No flash is touched. Peripheral writes are limited to the DE/panel registers
 * saved and restored here.
 */
#include "common.h"

typedef void (*fn0)(void);
#define REG(n) (*(volatile __sfr u32 *)(n))

#define DE_CTRL   0xc800u
#define DE_SCRN   0xc810u
#define DE_VIDSZ  0xc820u
#define DE_VIDCO  0xc824u
#define DE_VIDY   0xc828u
#define DE_VIDYS  0xc82cu
#define DE_VIDUV  0xc830u
#define DE_VIDUS  0xc834u
#define DE_CURY   0xc87cu
#define LCD_CON   0xa800u
#define LCD_STA   0xa804u

#define BSS_MAX   0x400000u   /* refuse an implausible .bss instead of looping */

__attribute__((section(".entry")))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    u32 i;
    for (i = 0; i < 40; ++i) out[i] = 0;

    u32 entry = in[0], beacon = in[1], bstart = in[2], bend = in[3];
    /* The beacon is read ONLY after the entry has run -- see below. Reading it
     * here returns the zeroed pre-run contents and reports "sensor not found"
     * for a run that actually succeeded. That mistake was made once. */

    if (entry < 0x02104000u || entry > 0x027F0000u || !beacon ||
        bstart >= bend || bend - bstart > BSS_MAX || (bstart & 3u) || (bend & 3u)) {
        out[0] = 0xBADu;
        finish(buffer, length, parameter, 40);
        return;
    }

    u32 s_ctl = REG(DE_CTRL), s_scrn = REG(DE_SCRN), s_vsz = REG(DE_VIDSZ);
    u32 s_vco = REG(DE_VIDCO), s_vy = REG(DE_VIDY), s_vys = REG(DE_VIDYS);
    u32 s_vuv = REG(DE_VIDUV), s_vus = REG(DE_VIDUS), s_cur = REG(DE_CURY);
    u32 s_lc = REG(LCD_CON), s_ls = REG(LCD_STA);

    /* Luma's own _start zeroes .bss; we enter past it, so do it here. */
    for (u32 a = bstart; a < bend; a += 4u) { *(volatile u32 *)a = 0u; }
    out[0] = 1u;

    ((void (*)(void *))entry)(buffer);   /* the entry may read the packet */

    /* EVERYTHING below is read AFTER the entry returns. Nothing may read a
     * beacon slot before this point, and no later loop may reuse an out[] slot
     * that a report field already occupies -- both of those bugs made a
     * successful sensor bring-up look like a failure. */
    /* Snapshot the WHOLE CSI register block into host-owned scratch RAM, while we
     * still own it. Reading the CSI registers after the run is useless (the stock
     * reconfigures them), but scratch RAM is ours and survives. Only 13 CSI
     * registers were ever captured in stock preview, and the one that decides
     * where CSI output is routed was not among them. */
    {
        u32 scratch = in[10];
        if (scratch) {
            for (i = 0; i < 128; ++i) {
                ((volatile u32 *)scratch)[i] = REG(0xb000u + 4u * i);
            }
        }
    }

    out[1] = *(volatile u32 *)(beacon + 0u);
    out[2] = *(volatile u32 *)(beacon + 4u);
    out[3] = *(volatile u32 *)(beacon + 8u);
    out[4] = *(volatile u32 *)(beacon + 12u);
    out[5] = *(volatile u32 *)(beacon + 16u);
    out[6] = *(volatile u32 *)(beacon + 20u);
    out[7] = *(volatile u32 *)(beacon + 24u);
    out[8] = *(volatile u32 *)(beacon + 28u);
    out[25] = *(volatile u32 *)(beacon + 32u);   /* ms clock delta */
    out[24] = *(volatile u32 *)(beacon + 36u);   /* keypad ladder min<<16 | max */
    out[9]  = *(volatile u32 *)(beacon + 40u);   /* software-IIC id ack  */
    out[10] = *(volatile u32 *)(beacon + 44u);   /* id byte             */
    out[11] = *(volatile u32 *)(beacon + 48u);   /* init table written  */
    out[17] = *(volatile u32 *)(beacon + 52u);   /* raw address ack     */
    out[18] = *(volatile u32 *)(beacon + 96u);   /* CSI b000            */
    out[19] = *(volatile u32 *)(beacon + 100u);  /* CSI b018            */
    out[20] = *(volatile u32 *)(beacon + 104u);  /* CSI b00c            */
    out[21] = *(volatile u32 *)(beacon + 108u);  /* CSI b014            */
    out[22] = *(volatile u32 *)(beacon + 60u);   /* ack after reg       */
    out[23] = *(volatile u32 *)(beacon + 64u);   /* ack after read addr */

    /* The capture planes, digested HERE, while Luma still owns them.
     *
     * This is the only trustworthy measurement. Reading the planes after
     * execute_verified() returns measures the STOCK firmware's state: a bounded
     * entry leaves interrupts enabled, so the stock resumes, re-owns the CSI
     * block and re-points its own buffers. A post-run read of b000 came back 0
     * after Luma had written 0x0440062d, which is what gave that away.
     *
     * Y is init-filled with 0x10 and UV with 0x80, so "captured data" is a byte
     * that is neither the fill value nor zero. */
    u32 py = in[8], puv = in[9];
    u32 y_data = 0, y_zero = 0, uv_data = 0, uv_zero = 0;
    if (py) {
        for (i = 0; i < 57600u; ++i) {
            unsigned char v = ((volatile unsigned char *)py)[i];
            if (v == 0u) { y_zero++; } else if (v != 0x10u) { y_data++; }
        }
    }
    if (puv) {
        for (i = 0; i < 28800u; ++i) {
            unsigned char v = ((volatile unsigned char *)puv)[i];
            if (v == 0u) { uv_zero++; } else if (v != 0x80u) { uv_data++; }
        }
    }
    out[28] = y_data;  out[29] = y_zero;
    out[30] = uv_data; out[31] = uv_zero;
    /* CSI servicing telemetry: the OR of every b00c value seen, how many polls
     * found events, and how many times the re-arm guard passed. */
    out[32] = *(volatile u32 *)(beacon + 112u);
    out[33] = *(volatile u32 *)(beacon + 116u);
    out[34] = *(volatile u32 *)(beacon + 120u);
    /* Which branch the vendor autodetect took. */
    out[35] = *(volatile u32 *)(beacon + 124u);
    out[36] = *(volatile u32 *)(beacon + 128u);
    out[37] = *(volatile u32 *)(beacon + 132u);
    out[38] = *(volatile u32 *)(beacon + 140u);   /* ops->mclk                       */
    out[39] = *(volatile u32 *)(beacon + 144u);   /* MCLK divider register 0x884c    */

    /* Frame-gated commit: wait for real commits so the panel actually shows
     * Luma's buffer. Polling DE_CTRL bit 1 gives exactly one frame per unit and
     * is bounded, unlike sleeping. */
    u32 frames = in[4];
    if (frames > 64u) { frames = 64u; }
    u32 maxguard = 0, done = 0;
    for (u32 f = 0; f < frames; ++f) {
        u32 guard;
        for (guard = 0; guard < 200000u && (REG(DE_CTRL) & 2u); ++guard) { }
        if (guard > maxguard) { maxguard = guard; }
        done = f + 1u;
        if (guard >= 200000u) { break; }
    }
    /* Recorded into their OWN slots. The original code wrote these over
     * out[17..23], which is where the sensor and CSI diagnostics live, so every
     * hold-frames run reported guard counters instead of registers. */
    out[26] = done;
    out[27] = maxguard;

    /* Put the display back exactly as found, then request a commit so the
     * restored address actually becomes active again. DE_CTRL bit 1 is a live
     * handshake bit: hardware raises it while an update is pending and clears it
     * at a frame boundary, so it must be compared masked off rather than as a
     * configuration bit. */
    REG(DE_VIDY) = s_vy;  REG(DE_VIDUV) = s_vuv;
    REG(DE_VIDYS) = s_vys; REG(DE_VIDUS) = s_vus;
    REG(DE_SCRN) = s_scrn; REG(DE_VIDSZ) = s_vsz; REG(DE_VIDCO) = s_vco;
    REG(DE_CTRL) = (s_ctl & ~2u) | 2u;          /* ask for the buffer commit */
    REG(LCD_CON) = s_lc;
    REG(LCD_STA) = s_ls;
    (void)s_cur;

    /* Drive the panel until the restored frame actually lands.
     *
     * Restoring the registers is not enough on its own: a DE commit is
     * frame-gated, so if we restore inside one frame the panel keeps the frame
     * we just showed. That is why a bounded run used to leave the camera frozen
     * on Luma's boot screen until a reset. So do what the stock frame interrupt
     * does -- acknowledge every LCDSTA event by writing the bit eight positions
     * higher, and kick -- and wait for the commit bit to clear each time. */
    for (u32 pass = 0; pass < 8u; ++pass) {
        u32 sta;
        REG(DE_CTRL) = (s_ctl & ~2u) | 2u;      /* re-request the commit */
        for (i = 0; i < 200000u && (REG(DE_CTRL) & 2u); ++i) { }
        sta = REG(LCD_STA) & 0xffu;
        for (i = 0; i < 8u; ++i) {
            if (sta & (1u << i)) { REG(LCD_STA) = 1u << (i + 8u); }
        }
        REG(LCD_CON) |= 2u;                     /* frame kick */
    }

    out[12] = ((REG(DE_CTRL) & ~2u) == (s_ctl & ~2u) &&
               REG(DE_SCRN) == s_scrn && REG(DE_VIDSZ) == s_vsz &&
               REG(DE_VIDY) == s_vy && REG(DE_VIDUV) == s_vuv &&
               REG(LCD_CON) == s_lc) ? 1u : 0u;
    out[13] = REG(DE_VIDY);
    out[14] = s_vy;
    out[15] = REG(DE_CTRL);
    out[16] = s_ctl;

    finish(buffer, length, parameter, 40);
}
