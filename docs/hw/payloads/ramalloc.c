/* Reserve a RAM block from the stock allocator for the Luma RAM run.
 *
 * The previous approach placed the bench image in the firmware's *free* pool and
 * snapshotted it for later restore. That cannot work: once the image is there the
 * running firmware starts handing that memory out, so the snapshot no longer
 * describes live state and the restore races the allocator.
 *
 * Instead this payload asks the stock allocator for a block and keeps it. The
 * block is then exclusively ours: the allocator will not hand it out, the
 * firmware holds no pointer into it, and nothing has to be restored afterwards.
 *
 * INPUT[0] = requested size in bytes (0 => 0x180000, one and a half MiB).
 * OUTPUT: base pointer, size, and a write test at both ends of the block.
 */
#include "common.h"
typedef void *(*alloc_t)(u32 size, u32 align);

/* sysMalloc, the same allocator entry the print payload already uses. It is the
 * firmware's own hal_sysMemMalloc front end, not a libc malloc. */
#define MALLOC_FN  0x0203E5C4u
#define DEFAULT_SIZE 0x180000u
#define PATTERN_A 0xA5A5A5A5u
#define PATTERN_B 0x5A5A5A5Au

__attribute__((section(".entry")))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    u32 i;
    for (i = 0; i < 26; ++i) out[i] = 0;

    u32 size = in[0] ? in[0] : DEFAULT_SIZE;
    size = (size + 31u) & ~31u;                 /* keep it aligned */
    alloc_t sysMalloc = (alloc_t)MALLOC_FN;

    void *p = sysMalloc(size, 32u);
    out[0] = (u32)p;
    out[1] = size;
    out[2] = 0;

    if (p != (void *)0 && (u32)p >= 0x02104000u && (u32)p + size <= 0x027F0000u) {
        /* Prove the whole block is really ours and writable, then leave it
         * non-zero so the RAM runner can show it was not already like this. */
        *(volatile u32 *)p = PATTERN_A;
        *(volatile u32 *)((u32)p + size - 4u) = PATTERN_B;
        out[2] = 1;
        out[3] = *(volatile u32 *)p;
        out[4] = *(volatile u32 *)((u32)p + size - 4u);
        out[5] = (u32)p + size;
    }

    finish(buffer, length, parameter, 6);
}
