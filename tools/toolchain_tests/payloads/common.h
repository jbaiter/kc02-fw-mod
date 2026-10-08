/* Freestanding KC02 RAM tests. No startup, peripheral writes or libc by default. */
#ifndef KC02_TEST_COMMON_H
#define KC02_TEST_COMMON_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;
#define NOINLINE __attribute__((noinline, noclone))
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#ifndef INPUT_OFFSET
#define INPUT_OFFSET 1792
#endif
#ifndef HEADER_OFFSET
#define HEADER_OFFSET 1856
#endif
#ifndef OUTPUT_OFFSET
#define OUTPUT_OFFSET 1888
#endif
#define DONE_MAGIC 0x5432304bUL
#define INPUT(b) ((volatile u32 *)((unsigned char *)(b) + INPUT_OFFSET))
#define OUTPUT(b) ((volatile u32 *)((unsigned char *)(b) + OUTPUT_OFFSET))
extern void CACHE_clean(void *, u32, u32);
static ALWAYS_INLINE void finish(void *buffer, u32 length, u32 parameter, u32 count)
{
    volatile u32 *h = (volatile u32 *)((unsigned char *)buffer + HEADER_OFFSET);
    h[1] = TEST_ID;
    h[2] = INPUT(buffer)[0];
    h[3] = length;
    h[4] = parameter;
    h[5] = count;
    h[6] = (u32)buffer;
    h[7] = INPUT(buffer)[1];
    __asm__ volatile ("" ::: "memory");
    h[0] = DONE_MAGIC;
    /* Publish CPU-written results for the stock USB transfer. Same vetted
       helper/third argument as RAM uploads; range is owned and 64-byte aligned. */
    CACHE_clean((unsigned char *)buffer + INPUT_OFFSET, 2048 - INPUT_OFFSET, 0);
}
#endif
