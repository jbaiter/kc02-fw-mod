/* Battery/charging verification, to be run right after a hardware repair.
   Calls only firmware read paths: the raw battery ADC getter and the status code that the
   print path derives its strobe bias from. INPUT[0] = 1 also reads the ADC a few times to
   show stability. Writes nothing but the packet + mailbox. */
#include "common.h"
typedef u32 (*get_adc_t)(void);
typedef int (*status_t)(void);
#define GET_ADC   0x020035B8u      /* getCurBatteryADC */
#define STATUS_FN 0x02059290u      /* status code used by kc02_print_head_ramp (8 = nominal, 14 = charging) */
#define MAILBOX   0x020DB300u
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    get_adc_t getADC = (get_adc_t)GET_ADC;
    status_t  status = (status_t)STATUS_FN;
    u32 n = in[0] ? (in[0] > 8u ? 8u : in[0]) : 4u;
    out[0] = status();                       /* 8 = nominal, 14 = charging, etc. */
    for (u32 k = 0; k < n; ++k) out[1 + k] = getADC();
    out[9] = ((int (*)(void))0x020034C4u)(); /* battery_read() as a cross-check */
    out[10] = *(volatile u32 *)(0x020D416Cu);
    volatile u32 *mb = (volatile u32 *)MAILBOX;
    mb[0] = 0x4D424F58u; mb[1] = out[0]; mb[2] = out[1]; mb[3] = out[9];
    CACHE_clean((void *)MAILBOX, 0x40, 0);
    finish(buffer, length, parameter, 3);
}
