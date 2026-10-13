/* Camera-mode probe: request camera mode through the firmware's own API, wait with the
   firmware's own RTOS delay, then publish the live sensor item + app context back.
   INPUT[0]=mode, INPUT[1]=param, INPUT[2]=address to read (sensor item), INPUT[3]=delay ms. */
#include "common.h"
typedef void (*ui_mode_request_t)(u32 mode, u32 param);
typedef void (*xos_dly_t)(u32 ticks);
typedef u32  (*xos_get_t)(void);
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    u32 mode = in[0], param = in[1], addr = in[2], dly = in[3];
    ui_mode_request_t ui_mode_request = (ui_mode_request_t)0x02008108u;
    xos_dly_t XOSTimeDly = (xos_dly_t)XOS_TIME_DLY;
    xos_get_t XOSTimeGet = (xos_get_t)XOS_TIME_GET;
    u32 t0 = XOSTimeGet();
    if (mode < 12) ui_mode_request(mode, param);
    if (dly) XOSTimeDly(dly);
    out[0] = mode;
    out[1] = XOSTimeGet() - t0;
    out[2] = addr;
    if (addr) {
        volatile u32 *p = (volatile u32 *)addr;
        for (u32 k = 0; k < 20; ++k) out[3 + k] = p[k];
    }
    out[23] = *(volatile u32 *)0x020D416Cu;   /* app_context[0] */
    out[24] = *(volatile u32 *)0x020CCC8Cu;   /* live dispatch table entry 0 */
    out[25] = *(volatile u32 *)0x020CCC90u;   /* live dispatch table entry 1 */
    finish(buffer, length, parameter, 1);
}
