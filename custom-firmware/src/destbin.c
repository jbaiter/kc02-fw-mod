/* SPDX-License-Identifier: MIT */
#include "destbin.h"
static u32 word(const u8 *p) { return p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static int magic(const u8 *p, const char *s)
{
    while (*s) { if (*p++!=(u8)*s++) { return 0; } } return 1;
}
DestBinResult destbin_inspect(const ImageSource *source, DestBinInfo *out)
{
    u8 b[512];
    u32 offset, crc=0xffffffffu;
    unsigned i, bit, sum=0;
    DestBinInfo info={DESTBIN_SIZE,0,DESTBIN_BOOT_SIZE,DESTBIN_APP_SIZE};
    if (!source || !out || !source->read || source->size!=DESTBIN_SIZE) { return DESTBIN_INVALID; }
    /* One ascending pass: a forward-only streaming source needs no seeking.
     * The BLDR header is in the first block and the SFAT marker at 0x304 is in
     * the second, so both are checked where the pass reaches them. */
    for (offset=0; offset<DESTBIN_SIZE; offset+=sizeof(b)) {
        if (!source->read(source->context,offset,b,sizeof(b))) { return DESTBIN_IO; }
        if (!offset) {
            for (i=0; i<sizeof(b); i++) { sum+=b[i]; }
            if ((sum&255) || !magic(b+4,"BLDR") || b[510]!=0x55 || b[511]!=0xaa ||
                word(b+0x20)!=0x02000000 || word(b+0x24)!=0x13 || word(b+0x28)!=0x644 ||
                word(b+0x30)!=0x01234567 || word(b+0x34)!=0x01234567) { return DESTBIN_INVALID; }
        }
        if (offset==0x200 && !magic(b+0x104,"SFAT")) { return DESTBIN_INVALID; }
        for (i=0; i<sizeof(b); i++) {
            crc^=b[i];
            for (bit=0; bit<8; bit++) { crc=(crc>>1)^((0u-(crc&1))&0xedb88320u); }
        }
    }
    info.crc=crc^0xffffffffu;
    *out=info;
    return DESTBIN_OK;
}
DestBinResult destbin_prefix_matches(const ImageSource *source, const ImageSource *running,
                                    u32 protected_bytes)
{
    u8 a[512], b[512];
    u32 offset;
    if (!source || !running || !source->read || !running->read ||
        source->size!=DESTBIN_SIZE || protected_bytes<DESTBIN_BOOT_SIZE ||
        protected_bytes>DESTBIN_SIZE || running->size<protected_bytes) { return DESTBIN_INVALID; }
    for (offset=0; offset<protected_bytes; offset+=sizeof(a)) {
        unsigned want=protected_bytes-offset, i;
        if (want>sizeof(a)) { want=sizeof(a); }
        if (!source->read(source->context,offset,a,want) ||
            !running->read(running->context,offset,b,want)) { return DESTBIN_IO; }
        for (i=0; i<want; i++) { if (a[i]!=b[i]) { return DESTBIN_PROTECTED; } }
    }
    return DESTBIN_OK;
}
