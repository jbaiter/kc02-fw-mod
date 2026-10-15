/* Stock KC02 firmware reference: the USB Bulk-Only Transport service.
 *
 * Decompiled from firmware/original.bin. The controller driver is in the SDK
 * (ax32xx/src/ax32xx_usb.c); this is the MSC service layer the firmware actually
 * runs, which is what the RAM hoist calls on every frame.
 */
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define REG(n) (*(volatile u32 *)(n))

/* ---------------------------------------------------------------------------
 * 0x0204d0a8  rbc_process()   -- annotated as bot_service_once
 *
 * Called from the every-mode frame hook (0x02000404, the address the RAM hoist
 * patches) so the stock firmware services MSC continuously. Returns 1 when there
 * is nothing to do.
 *
 *   rbc_rec_pkg()          check the receive package / endpoint state
 *   get_cbw()              fetch a command block; 0 -> arm the status byte 0x10
 *   usb_scsi_dispatch()    otherwise dispatch the CBW
 *
 * That is the whole loop. The hoist stubs into this so a RAM payload can keep
 * using the vendor command while Luma runs.
 * ------------------------------------------------------------------------ */
int rbc_process(void)
{
    int rc = rbc_rec_pkg();
    if (rc == 0) { return 1; }
    if (get_cbw() == 0) { REG(0xa058) = 0x10; return rc; }
    return usb_scsi_dispatch();
}

/* ---------------------------------------------------------------------------
 * 0x0204d014  rbc_rec_pkg()   -- annotated as bot_cbw_ready
 *
 * Interrupt-masked receive check. Note 0xf is the USB interrupt in the enable
 * mask, disabled around the check and re-enabled after.
 *
 * The return expression is a sign trick worth reading carefully: it yields 1 only
 * when (a058 & 1) is set AND (a064 == 0) AND a060 is non-zero, i.e. a package is
 * present and the previous one has been consumed.
 * ------------------------------------------------------------------------ */
u32 rbc_rec_pkg(void)
{
    ax32xx_intEnable(0xf, 0);
    REG(0xa038) = g_020d7582_11803 & 0x7f;
    u32 ready = 0;
    if ((REG(0xa058) & 1) && REG(0xa064) == 0) {
        ready = ((-(REG(0xa060) ^ 0x1f) | (REG(0xa060) ^ 0x1f)) ^ 0xffffffffu) >> 31;
    }
    ax32xx_intEnable(0xf, 1);
    return ready;
}

/* ---------------------------------------------------------------------------
 * Symbols in the manifest that do NOT match Ghidra's own identification.
 *
 *   tools/ghidra_symbols.tsv says  0x02021648 = usb_reenumerate
 *   Ghidra says                    0x02021648 = XMsgQPost (xos/xmsgq.h)
 *
 * XMsgQPost is a ring-buffer post: it writes a message pointer at
 * base[4] + head*4, advances head, and wraps when head passes tail. That is a
 * message queue, not a USB re-enumeration. One of the two is wrong and it should
 * be settled before either is relied on -- the manifest is curated by hand, so
 * this is a candidate for a bad annotation rather than a decompiler error.
 * ------------------------------------------------------------------------ */

/* ---------------------------------------------------------------------------
 * 0x02021648  XMsgQPost(mque, msg) -- as Ghidra reads it.
 * ------------------------------------------------------------------------ */
int XMsgQPost(u8 *mque, void *msg)
{
    if (mque == 0 || mque[0] == 0 || mque[1] == mque[0]) { return 0xff; }
    signed char head = (signed char)mque[3];
    mque[3] = (u8)(head + 1);
    *(void **)(*(int *)(mque + 4) + (u32)head * 4) = msg;
    mque[1] = (u8)(mque[1] + 1);
    if ((signed char)mque[0] <= (signed char)mque[3]) { mque[3] = 0; }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Related manifest entries, for orientation:
 *   0x0204abc8  bot_service_wrapper   panel-feeding wrapper (NOT the pure service)
 *   0x0204d0a8  bot_service_once      rbc_process above
 *   0x0204d014  bot_cbw_ready         rbc_rec_pkg above
 *   0x0202b058  dcache_clean_range    the cache flush the tooling depends on
 *   0x02021648  usb_reenumerate       <-- does not match Ghidra; see above
 *
 * The tooling uses 0x0204d0a8 (not the wrapper) precisely because the wrapper also
 * feeds the panel, which is not safe to call from a bench payload.
 * ------------------------------------------------------------------------ */
