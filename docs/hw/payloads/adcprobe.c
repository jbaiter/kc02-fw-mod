/* Measure a completed keypad conversion using the stock HAL, fixed channel 3.
 * The HAL briefly writes the ADC control/divider, as normal keypad polling does.
 * This is NOT a passive readback. No flash, UI, GPIO, timer or sensor changes.
 * OUTPUT: control before, returned sample, control after, divider, data. */
#include "common.h"
typedef u32 (*adc_get_t)(u32 channel);
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *out = OUTPUT(buffer);
    out[0] = *(volatile __sfr u32 *)0x9170;
    out[1] = ((adc_get_t)0x0203814cu)(3);
    out[2] = *(volatile __sfr u32 *)0x9170;
    out[3] = *(volatile __sfr u32 *)0x9174;
    out[4] = *(volatile __sfr u32 *)0x9178;
    finish(buffer, length, parameter, 5);
}
