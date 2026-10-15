/* Bounded DE register/latch test; RAM-only, no flash, no mode request.
 *
 * Purpose: Luma's present() writes only bit 0 of DE_FRAME_CTRL (0xc800). The
 * stock SDK's ax32xx_deUpdate() sets bit 1, and stock preview readback showed
 * c800 == 3. This probe decides whether bit 1 is the double-buffer latch that
 * makes a new video address take effect, and reports whether hardware accepts
 * Luma's geometry values, without allocating a framebuffer.
 *
 * INPUT[0] = 0 : inspect only, no writes at all.
 * INPUT[0] = 1 : staged-write and bounded latch test, then restore.
 *
 * The latch test points DE video Y at the bench packet area. Only reads happen
 * there, so it cannot corrupt the packet; the original active address is
 * restored and re-latched inside the same call. Worst case is a few frames of
 * wrong image, and the stock app re-programs DE afterwards anyway.
 *
 * OUTPUT = revision, {selector:value} inspection words, then latch-test words.
 */
#include "common.h"
#define REG(n) (*(volatile __sfr u32 *)(n))

#define DE_CTRL   0xc800u   /* bit0 enable, bit1 update/latch handshake */
#define DE_SCRN   0xc810u
#define DE_VIDSZ  0xc820u
#define DE_VIDCO  0xc824u
#define DE_VIDY   0xc828u
#define DE_VIDYS  0xc82cu
#define DE_VIDUV  0xc830u
#define DE_VIDUS  0xc834u
#define DE_CURY   0xc87cu
#define LCD_STA   0xa804u
#define LCD_W     0xa840u
#define LCD_H     0xa844u
#define CSI_INT   0xb014u
#define CSI_SEN   0xb01cu
#define ROT_CON   0x898cu
#define ROT_TGY   0x8998u

#define SCRATCH   0x020d8800u  /* bench packet base: harmless to read */
typedef void (*delay_t)(u32);
#define SLEEP_MS  0x020218b4u  /* pinned stock XOS delay, already proven */

__attribute__((section(".entry")))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *out = OUTPUT(buffer);
    u32 i;

    for (i = 0; i < 26; ++i) out[i] = 0;

    u32 ctrl = REG(DE_CTRL);
    u32 cury = REG(DE_CURY);
    out[0] = 1;                 /* probe revision */
    out[1] = ctrl;
    out[2] = REG(DE_SCRN);
    out[3] = REG(DE_VIDSZ);
    out[4] = REG(DE_VIDCO);
    out[5] = REG(DE_VIDY);
    out[6] = REG(DE_VIDYS);
    out[7] = REG(DE_VIDUV);
    out[8] = REG(DE_VIDUS);
    out[9] = cury;
    out[10] = REG(LCD_STA);
    out[11] = REG(LCD_W);
    out[12] = REG(LCD_H);
    out[13] = REG(CSI_INT);
    out[14] = REG(CSI_SEN);
    out[15] = REG(ROT_CON);
    out[16] = REG(ROT_TGY);

    if (INPUT(buffer)[0] != 1) { finish(buffer, length, parameter, 17); return; }

    /* Staged write with no DE_CTRL write: nothing should latch yet. */
    REG(DE_VIDY) = SCRATCH;
    out[17] = REG(DE_CURY);

    /* Ask for the update, then give the display time to reach a frame boundary.
     * A register-poll only spans microseconds, which cannot cross a frame. */
    u32 target = SCRATCH & 0x00ffffffu;
    REG(DE_CTRL) = (ctrl & 1u) | 2u;
    ((delay_t)SLEEP_MS)(20);
    out[18] = REG(DE_CURY);
    out[19] = REG(DE_CTRL);   /* bit1 still set => commit is frame-gated */

    /* Restore the original active address and re-latch. */
    REG(DE_VIDY) = cury;
    REG(DE_CTRL) = (ctrl & 1u) | 2u;
    ((delay_t)SLEEP_MS)(20);
    out[20] = REG(DE_CURY);
    out[21] = REG(DE_CTRL);
    out[22] = target;         /* what we asked for, for comparison */
    out[23] = REG(DE_VIDY);
    out[24] = REG(DE_SCRN);   /* sanity: screen config untouched */

    finish(buffer, length, parameter, 25);
}
