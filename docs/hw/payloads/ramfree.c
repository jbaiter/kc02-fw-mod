/* Release a RAM block taken by ramalloc.
 *
 * The RAM-run tool allocates a block per run and the stock pool is only ~6.98 MB,
 * so a dozen runs exhaust it and every later allocation fails. This frees the
 * block so runs do not leak.
 *
 * INPUT[0] = block address to release.
 * OUTPUT: the address passed, and the allocator's free entry point used.
 */
#include "common.h"
typedef void (*free_t)(void *p);
#define FREE_FN 0x0203E694u   /* hal_sysMemFree */

__attribute__((section(".entry")))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    u32 i;
    for (i = 0; i < 26; ++i) out[i] = 0;

    u32 addr = in[0];
    out[0] = addr;
    out[1] = (u32)FREE_FN;
    out[2] = 0;

    /* Refuse anything outside the allocator pool rather than hand a stray
     * pointer to the firmware's free list. */
    if (addr >= 0x02104000u && addr < 0x027F0000u) {
        ((free_t)FREE_FN)((void *)addr);
        out[2] = 1;
    }
    finish(buffer, length, parameter, 3);
}
