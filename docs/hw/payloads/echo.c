/* Harmless proof payload: computes from the packet and reads one firmware word.
   INPUT[0]=seed, INPUT[2]=address to read. Publishes results via the vetted finish(). */
#include "common.h"
#ifndef TEST_ID
#define TEST_ID 0xEC401000
#endif
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    u32 seed = in[0];
    u32 addr = in[2];
    u32 val = 0;
    if (addr) val = *(volatile u32 *)addr;      /* read-only firmware probe */
    out[0] = seed ^ 0x5A5A5A5AU;
    out[1] = val;
    out[2] = addr;
    finish(buffer, length, parameter, 2);
}
