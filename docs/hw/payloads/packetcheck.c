/* CPU-side FNV-1a of the complete incoming execution packet, before writing
 * any result/header. Host independently hashes input.bin for comparison.
 * No peripheral access or mode change. */
#include "common.h"
void test_entry(void *buffer, u32 length, u32 parameter)
{
    const volatile unsigned char *p = buffer;
    u32 hash = 2166136261u;
    for (u32 i = 0; i < length; ++i) hash = (hash ^ p[i]) * 16777619u;
    OUTPUT(buffer)[0] = hash;
    OUTPUT(buffer)[1] = length;
    finish(buffer, length, parameter, 2);
}
