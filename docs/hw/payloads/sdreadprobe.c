/* Read-only SD probe. No filesystem open/create, no sector writes, no flash.
 * Saves sector 0 and (if MBR partition 0 is present) its boot sector in the
 * packet's first 1024 bytes, below the input/header/output ABI areas.
 */
#include "common.h"
#define SD_READ 0x0203d05cu
#define SD_STOP 0x0203c6e8u
#define SD_CAPACITY 0x0203cf44u
#define FS_WRITER_PTR 0x020f2f0cu
#define FS_READER_PTR 0x020f2f18u
static u32 le32(const unsigned char *p)
{
    return p[0] | (u32)p[1]<<8 | (u32)p[2]<<16 | (u32)p[3]<<24;
}
__attribute__((section(".entry")))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *out=OUTPUT(buffer);
    unsigned char *b=buffer;
    u32 i,lba=0;
    int ret;
    for (i=0;i<26;i++) out[i]=0;
    out[0]=*(volatile u32 *)FS_READER_PTR;
    out[1]=*(volatile u32 *)FS_WRITER_PTR;
    if (out[0]!=SD_READ || out[1]!=0x0203cf8cu) {
        out[25]=1;
        finish(buffer,length,parameter,26);
        return;
    }
    out[2]=((u32 (*)(void))SD_CAPACITY)();
    if (!out[2]) {
        out[25]=2;
        finish(buffer,length,parameter,26);
        return;
    }
    ret=((int (*)(void *,u32,u32))SD_READ)(b,0,1);
    out[3]=(u32)ret;
    if (ret<0) {
        out[4]=(u32)((int (*)(void))SD_STOP)();
        out[25]=3;
        finish(buffer,length,parameter,26);
        return;
    }
    if (b[510]==0x55 && b[511]==0xaa && b[0]!=0xeb && b[0]!=0xe9)
        lba=le32(b+454);
    out[5]=lba;
    if (lba>=out[2]) {
        out[25]=4;
    } else {
        out[6]=(u32)((int (*)(void *,u32,u32))SD_READ)(b+512,lba,1);
    }
    out[7]=(u32)((int (*)(void))SD_STOP)();
    finish(buffer,length,parameter,26);
}
