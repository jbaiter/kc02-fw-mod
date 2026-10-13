/* Mode driver: calls the firmware's own ui_mode_request(INPUT[0], INPUT[1]).
   Address/signature are the app API every mode change in the firmware goes through. */
#include "common.h"
#ifndef TEST_ID
#define TEST_ID 0x0DE40100
#endif
typedef void (*ui_mode_request_t)(u32 mode, u32 param);
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    u32 mode = in[0];
    u32 param = in[1];
    ui_mode_request_t ui_mode_request = (ui_mode_request_t)0x02008108u;
    if (mode < 12) ui_mode_request(mode, param);
    out[0] = mode;
    out[1] = param;
    out[2] = 0xA5A50000u | mode;
    finish(buffer, length, parameter, 1);
}
