/* SPDX-License-Identifier: MIT */
#include "update.h"
static u32 get32(const u8 *b) { return b[0]|((u32)b[1]<<8)|((u32)b[2]<<16)|((u32)b[3]<<24); }
static void put32(u8 *b, u32 n) { unsigned i; for (i=0; i<4; i++) { b[i]=n>>(i*8); } }
static u32 crc_step(u32 crc, const u8 *b, unsigned size)
{
    unsigned i;
    while (size--) {
        crc^=*b++;
        for (i=0; i<8; i++) { crc=(crc>>1)^(0xedb88320u&(0u-(crc&1))); }
    }
    return crc;
}
static int region_crc(ByteRead read, void *context, u32 offset, u32 size, u32 expected)
{
    u8 bytes[256];
    u32 crc=~0u;
    while (size) {
        unsigned n=size>sizeof(bytes) ? sizeof(bytes) : size;
        if (!read || !read(context,offset,bytes,n)) { return 0; }
        crc=crc_step(crc,bytes,n);
        size-=n; offset+=n;
    }
    return ~crc==expected;
}
int image_validate(const ImageSource *s, u32 board_bit, ImageInfo *out)
{
    static const u8 magic[8]={'L','U','M','A','I','M','G',0};
    u8 b[IMAGE_HEADER];
    ImageInfo info;
    u32 checksum;
    unsigned i;
    if (!out || !s || !s->read || s->size<IMAGE_HEADER ||
        (board_bit!=1 && board_bit!=2 && board_bit!=4) ||
        !s->read(s->context,0,b,sizeof(b))) { return 0; }
    for (i=0; i<8; i++) { if (b[i]!=magic[i]) { return 0; } }
    if (b[8]!=1 || b[9] || b[10]!=IMAGE_HEADER || b[11]) { return 0; }
    info.boards=get32(b+12); info.bytes=get32(b+16); info.entry=get32(b+24);
    info.version=get32(b+28); info.crc=get32(b+32); checksum=get32(b+36);
    if (!(info.boards&board_bit) || (info.boards&~7u) || !info.bytes || info.bytes>SLOT_SIZE ||
        info.bytes!=s->size-IMAGE_HEADER || get32(b+20)!=0x02000000 ||
        info.entry!=0x02000000 || !info.version || get32(b+40)) { return 0; }
    for (i=44; i<IMAGE_HEADER; i++) { if (b[i]) { return 0; } }
    put32(b+36,0);
    if (crc32(b,sizeof(b))!=checksum || !region_crc(s->read,s->context,IMAGE_HEADER,info.bytes,info.crc)) { return 0; }
    *out=info;
    return 1;
}
static int read_record(const Nor *nor, unsigned slot, BootRecord *r)
{
    static const u8 magic[8]={'L','M','C','O','M','M','I','T'};
    u8 b[32];
    unsigned i;
    if (!nor->read || !nor->read(nor->context,slot ? META_B : META_A,b,sizeof(b))) { return 0; }
    for (i=0; i<8; i++) { if (b[i]!=magic[i]) { return 0; } }
    if (crc32(b,28)!=get32(b+28)) { return 0; }
    r->generation=get32(b+8); r->slot=get32(b+12); r->bytes=get32(b+16);
    r->crc=get32(b+20); r->version=get32(b+24);
    if (!r->generation || r->slot!=slot || !r->version || !r->bytes || r->bytes>SLOT_SIZE) { return 0; }
    return region_crc(nor->read,nor->context,slot ? SLOT_B : SLOT_A,r->bytes,r->crc);
}
int boot_select(const Nor *nor, BootRecord *out)
{
    BootRecord a,b;
    int valid_a,valid_b;
    if (!nor || !out) { return 0; }
    valid_a=read_record(nor,0,&a); valid_b=read_record(nor,1,&b);
    if ((!valid_a && !valid_b) || (valid_a && valid_b && a.generation==b.generation)) { return 0; }
    *out=!valid_a ? b : !valid_b ? a : a.generation>b.generation ? a : b;
    return 1;
}
UpdateResult update_apply(const Nor *nor, const ImageSource *s, u32 board_bit,
                          int physical_confirmation, int recovery_verified)
{
    ImageInfo info;
    BootRecord current;
    u8 bytes[256], record[32];
    unsigned slot, offset, i;
    u32 base, meta;
    if (!physical_confirmation || !recovery_verified || !nor || !nor->read || !nor->erase || !nor->program) { return UPDATE_DENIED; }
    if (!image_validate(s,board_bit,&info)) { return UPDATE_INVALID; }
    if (!boot_select(nor,&current)) { return UPDATE_NO_BOOT; }
    if (info.version<=current.version || current.generation==0xffffffffu) { return UPDATE_STALE; }
    slot=current.slot^1u; base=slot ? SLOT_B : SLOT_A; meta=slot ? META_B : META_A;
    /* Only the inactive application slot is touched. Never boot/recovery or
     * active metadata. The source may be SD or a fully staged USB upload. */
    for (offset=0; offset<info.bytes; offset+=NOR_SECTOR) {
        if (!nor->erase(nor->context,base+offset)) { return UPDATE_IO; }
    }
    for (offset=0; offset<info.bytes;) {
        unsigned n=info.bytes-offset>sizeof(bytes) ? sizeof(bytes) : info.bytes-offset;
        if (!s->read(s->context,IMAGE_HEADER+offset,bytes,n) ||
            !nor->program(nor->context,base+offset,bytes,n)) { return UPDATE_IO; }
        offset+=n;
    }
    if (!region_crc(nor->read,nor->context,base,info.bytes,info.crc)) { return UPDATE_IO; }
    for (i=0; i<sizeof(record); i++) { record[i]=0; }
    record[0]='L'; record[1]='M'; record[2]='C'; record[3]='O'; record[4]='M'; record[5]='M'; record[6]='I'; record[7]='T';
    put32(record+8,current.generation+1); put32(record+12,slot);
    put32(record+16,info.bytes); put32(record+20,info.crc); put32(record+24,info.version);
    put32(record+28,crc32(record,28));
    if (!nor->erase(nor->context,meta) || !nor->program(nor->context,meta,record,sizeof(record))) { return UPDATE_IO; }
    /* If the commit completed but the response was lost, boot_select reports
     * the new version on reconnect; never blindly retry a mutating command. */
    if (!read_record(nor,slot,&current)) { return UPDATE_IO; }
    return UPDATE_OK;
}
