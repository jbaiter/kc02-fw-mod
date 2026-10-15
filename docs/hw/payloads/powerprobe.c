/* Read the stock idle-timeout configuration and current RAM counters.
 * INPUT[0] == 1 resets ONLY the RAM idle accumulator (the same word cleared
 * by tick_auto_poweroff(1)). No config setter, flash, mode request or battery
 * protection change. OUTPUT: mode, timeout seconds, accumulator before/after,
 * last idle tick, current XOS ms, reset requested, two other shutdown
 * countdowns, cached battery ADC, battery/charging status, app power word.
 * Status 14 means charging, NOT battery absent. */
#include "common.h"
typedef u32 (*get_config_t)(u32);
typedef u32 (*get_time_t)(void);
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *out = OUTPUT(buffer);
    volatile u32 *idle = (volatile u32 *)0x020ccdccu;
    out[0] = *(volatile u32 *)0x020ccd54u;
    out[1] = ((get_config_t)0x020064b4u)(8);
    out[2] = *idle;
    if (INPUT(buffer)[0] == 1) *idle = 0;
    out[3] = *idle;
    out[4] = *(volatile u32 *)0x020ccdd0u;
    out[5] = ((get_time_t)0x02021894u)();
    out[6] = INPUT(buffer)[0] == 1;
    out[7] = *(volatile u32 *)0x020ccd44u;
    out[8] = *(volatile u32 *)0x020ccd48u;
    out[9] = ((get_time_t)0x020035b8u)();
    out[10] = ((get_time_t)0x02059290u)();
    out[11] = *(volatile u32 *)0x020d416cu;
    finish(buffer, length, parameter, 12);
}
