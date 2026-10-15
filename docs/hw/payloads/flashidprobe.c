/* Read-only JEDEC 0x9F via the stock HAL. The runner pins the HAL's full
 * 96-byte body before execution. No WREN, erase/program or address-window read.
 * OUTPUT[0] = manufacturer/type/capacity in the low 24 bits. */
#include "common.h"
typedef u32 (*read_id_t)(void);
void test_entry(void *buffer, u32 length, u32 parameter)
{
    OUTPUT(buffer)[0] = ((read_id_t)0x0203d1fcu)();
    finish(buffer, length, parameter, 1);
}
