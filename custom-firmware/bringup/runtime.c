/* SPDX-License-Identifier: MIT
 * Freestanding compiler support. No allocator, I/O or application callbacks.
 */
#include "luma.h"
void *memcpy(void *destination, const void *source, size_t size)
{
    u8 *out=destination;
    const u8 *in=source;
    size_t i;
    for (i=0; i<size; i++) { out[i]=in[i]; }
    return destination;
}
void *memset(void *destination, int value, size_t size)
{
    u8 *out=destination;
    size_t i;
    for (i=0; i<size; i++) { out[i]=(u8)value; }
    return destination;
}
