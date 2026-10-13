/* Camera-mode probe with a RAM mailbox.
   INPUT[0]=mode INPUT[1]=param INPUT[2]=address to sample INPUT[3]=total wait ms.
   Requests the mode repeatedly while yielding to the RTOS (so the mode manager runs),
   then copies live state into MAILBOX and cache-cleans it for the DMA read path.
   The USB device may go away during this; the mailbox survives as long as the cable
   stays connected and the CPU keeps running. */
#include "common.h"
typedef void (*ui_mode_request_t)(u32 mode, u32 param);
typedef void (*xos_dly_t)(u32 ticks);
#define MAILBOX   0x020DB300u
#define MB_MAGIC  0x4D424F58u
#define MB_WORDS  0x40
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    u32 mode = in[0], param = in[1], addr = in[2], total = in[3];
    ui_mode_request_t ui_mode_request = (ui_mode_request_t)0x02008108u;
    xos_dly_t XOSTimeDly = (xos_dly_t)XOS_TIME_DLY;
    volatile u32 *mb = (volatile u32 *)MAILBOX;
    u32 waited = 0, req = 0;
    for (u32 i = 0; i < MB_WORDS; ++i) mb[i] = 0xDEADBEEFu;
    mb[0] = MB_MAGIC; mb[1] = 0; mb[2] = mode; mb[3] = addr;
    CACHE_clean((void *)MAILBOX, MB_WORDS * 4, 0);
    while (waited < total) {
        if (mode < 12) { ui_mode_request(mode, param); req++; }
        XOSTimeDly(50); waited += 50;
    }
    mb[1] = req; mb[4] = waited;
    if (addr) {
        volatile u32 *p = (volatile u32 *)addr;
        for (u32 k = 0; k < 20; ++k) mb[8 + k] = p[k];      /* sensor item, 80 bytes */
    }
    mb[32] = *(volatile u32 *)0x020D416Cu;                  /* app_context[0..3] */
    mb[33] = *(volatile u32 *)0x020D4170u;                  /* app_context[4..7] */
    mb[34] = *(volatile u32 *)0x020CCC90u;                  /* dispatch[1] */
    mb[35] = *(volatile u32 *)0x020D41E0u;                  /* ui queue word 0 */
    mb[36] = (u32)buffer; mb[37] = length; mb[38] = parameter;
    CACHE_clean((void *)MAILBOX, MB_WORDS * 4, 0);
    finish(buffer, length, parameter, req);
}
