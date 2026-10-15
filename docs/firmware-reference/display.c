/* Stock KC02 firmware reference: display (DE video layer and LCD frame service).
 *
 * Decompiled from firmware/original.bin. The SDK carries the vendor display code
 * (ax32xx/osd/ax32xx_osd.c, ax32xx/lcd/ax32xx_lcd.c); what is recorded here is the
 * wrapper the firmware actually uses plus the register contract as MEASURED on
 * hardware, because the measured values are what any port has to match.
 */
#include <stdint.h>

typedef uint8_t  u8;
typedef uint32_t u32;

#define REG(n) (*(volatile u32 *)(n))

/* ---------------------------------------------------------------------------
 * 0x02035814  ax32xx_lcdKick()
 *
 * The whole function. The stock firmware calls it from its LCD frame interrupt on
 * every frame event, and it is the reason a polling application shows nothing: a
 * DE commit is frame-gated, so without a kick no frame boundary is produced and
 * the panel keeps scanning whatever buffer was last latched.    VERIFIED
 * ------------------------------------------------------------------------ */
void ax32xx_lcdKick(void)
{
    if (g_lcd_frame_gate == 0) { REG(0xa800) |= 2; }
}

/* ---------------------------------------------------------------------------
 * LCD frame events, from the handler at 0x0203584c.
 *
 * LCDSTA (0xa804) event bits are acknowledged by writing the SAME bit eight
 * positions higher. Bit 8 is the frame/vsync event and its callback is what runs
 * lcdKick() -- but only when (a800 & 0x30) == 0x10, which is NOT the case in
 * stock USB mode (measured a800 = 0x31, so 0x30). Luma's equivalent therefore
 * kicks unconditionally rather than copying that gate.              VERIFIED
 * ------------------------------------------------------------------------ */
#define LCD_CON 0xa800u
#define LCD_STA 0xa804u

static void lcd_service(void)
{
    for (;;) {
        u32 events = REG(LCD_STA) & 0xffu;          /* event bits live in the low byte */
        if (!events) { break; }
        for (unsigned bit = 0; bit < 8; ++bit) {
            if (events & (1u << bit)) { REG(LCD_STA) = 1u << (bit + 8u); }
        }
    }
    REG(LCD_CON) |= 2;                              /* the kick */
}

/* ---------------------------------------------------------------------------
 * MEASURED DE / LCD register contract, stock USB mode.
 *
 * These are readbacks, not reset values. They are the target any port has to
 * match, and two of them were got wrong at first:
 *
 *   0xc800 = 0x00000001   DE frame control. bit 0 enable, bit 1 commit-request.
 *                         In steady state the commit bit is CLEAR; it is raised
 *                         only when an address changes, then self-clears at a
 *                         frame boundary.
 *   0xc810 = 0x013f00ef   screen 240x320  ((height-1)<<16 | (width-1))
 *   0xc818 = 0x2ce52083   DE display control. NEVER assign this wholesale: it
 *                         carries layer enables and fields a port does not model,
 *                         and writing 1 silently turns the panel path off.
 *   0xc820 = 0x013f00ef   video size 240x320
 *   0xc828 = 0x0015a480   video Y address, a 24-BIT SDRAM OFFSET, not a pointer
 *   0xc82c = 0x00000040   video Y stride, in WORDS (0x40 -> 256 bytes)
 *   0xc830 = 0x0016e480   video UV address, same 24-bit convention
 *   0xc834 = 0x00000040   UV stride, words
 *   0xc87c = (masked 24-bit) the ACTIVE Y address; ax32xx_videoGetYAddr() returns
 *                         this & 0xffffff
 *   0xa800 = 0x31         LCD_CON. Never assign: writing 1 clears the panel bits.
 *   0xa804 = 0x80980000   LCDSTA; bit 31 is NOT a constant ready flag, it was
 *                         seen both set and clear.                  VERIFIED
 *
 * The stride asymmetry is a real trap: DE strides are in words, CSI strides are
 * in bytes (see csi.c, b068 = 320).
 * ------------------------------------------------------------------------ */

/* 0x020336dc  ax32xx_videoSetAddr(y, uv, update)
 * Note it stores the FULL pointer; the 24-bit form is what the register reads
 * back, so the hardware truncates rather than the driver. */
void ax32xx_videoSetAddr(u32 y, u32 uv, u8 update)
{
    REG(0xc828) = y;
    REG(0xc830) = uv;
    (void)update;
}

/* 0x0203367c / 0x020336ac  the active addresses, masked. */
u32 ax32xx_videoGetYAddr(void)  { return REG(0xc87c) & 0xffffffu; }
u32 ax32xx_videoGetUVAddr(void) { return REG(0xc830) & 0xffffffu; }

/* 0x0202b468 area -- DE update. Setting bit 1 requests the buffer commit; the
 * hardware clears it at a frame boundary, which is why every
 * "write it and read it back immediately" check fails without a frame wait. */
void ax32xx_deUpdate(void) { REG(0xc800) |= (1u << 1); }

/* ---------------------------------------------------------------------------
 * What the stock firmware does NOT do, and matters:
 *
 * The stock display path composes through the ROTATION ENGINE, with the
 * DE video plane as its destination (measured ROTATECON = 5 and ROTTGYADR ==
 * 0xc828). A port that renders directly into the DE plane must disable the engine
 * or it will keep overwriting that plane -- which shows up as a bright block
 * rolling across the UI. See rotate_pip.c.
 * ------------------------------------------------------------------------ */
