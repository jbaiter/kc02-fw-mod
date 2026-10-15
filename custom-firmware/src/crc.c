/* SPDX-License-Identifier: MIT
 * The one CRC32 in the firmware. It is declared in luma.h, so it lives here
 * rather than inside whichever file happened to need it first: an updater that
 * has to link the theme parser to check a checksum is a bad trade.
 */
#include "luma.h"
u32 crc32(const u8 *p, unsigned size)
{
    u32 crc=~0u;
    unsigned i;
    while (size--) {
        crc^=*p++;
        for (i=0; i<8; i++) { crc=(crc>>1)^(0xedb88320u&(0u-(crc&1))); }
    }
    return ~crc;
}
