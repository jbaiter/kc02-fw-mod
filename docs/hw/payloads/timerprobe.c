/* Reversible TT test, only when TTCTL == 0 (unused by stock USB).
 * Tests SDK start/reset 0x42, stop 0, resume 2 and stop/reset 0x40.
 * A fixed 50 ms stock XOS delay estimates clock rate; no mode request.
 * Restores all three TT registers, does not enable any IRQ.
 * OUTPUT: original ctl/pr/cnt; start ctl/count; count after 50ms/elapsed ms;
 * stop ctl/count/count after loop; resume ctl/count/count after loop;
 * reset ctl/count/count after loop; restored ctl/pr/cnt (19 words). */
#include "common.h"
#define REG(n) (*(volatile __sfr u32 *)(n))
typedef void (*delay_t)(u32);
typedef u32 (*time_t)(void);
static void delay(void)
{
    for (volatile u32 i = 0; i < 10000; ++i) __asm__ volatile ("l.nop");
}
__attribute__((section(".entry")))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *out = OUTPUT(buffer);
    u32 ctl = REG(0x9900), pr = REG(0x9904), cnt = REG(0x9908);
    time_t now = (time_t)0x02021894u;
    delay_t sleep_ms = (delay_t)0x020218b4u;
    out[0] = ctl; out[1] = pr; out[2] = cnt;
    if (ctl != 0) { finish(buffer, length, parameter, 3); return; }
    REG(0x9904) = 0xffffffff;
    REG(0x9908) = 0;
    REG(0x9900) = 0x42;
    out[3] = REG(0x9900);
    out[4] = REG(0x9908);
    u32 start_ms = now();
    sleep_ms(50);
    out[5] = REG(0x9908);
    out[6] = now() - start_ms;
    REG(0x9900) = 0;
    out[7] = REG(0x9900);
    out[8] = REG(0x9908);
    delay();
    out[9] = REG(0x9908);
    REG(0x9900) = 2;
    out[10] = REG(0x9900);
    out[11] = REG(0x9908);
    delay();
    out[12] = REG(0x9908);
    REG(0x9900) = 0x40;
    out[13] = REG(0x9900);
    out[14] = REG(0x9908);
    delay();
    out[15] = REG(0x9908);
    REG(0x9908) = cnt;
    REG(0x9904) = pr;
    REG(0x9900) = ctl;
    out[16] = REG(0x9900); out[17] = REG(0x9904); out[18] = REG(0x9908);
    finish(buffer, length, parameter, 19);
}
