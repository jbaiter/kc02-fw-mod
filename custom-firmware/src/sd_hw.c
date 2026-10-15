/* SPDX-License-Identifier: MIT */
#include "sd_hw.h"
#define REG(n) (*(volatile __sfr u32 *)(n))
static int wait_bit(unsigned reg, u32 mask)
{
    unsigned timeout=200000;
    while (!(REG(reg)&mask) && --timeout) { }
    return timeout != 0;
}
static int command(unsigned cmd, u32 arg, u32 *response)
{
    REG(0x9884)=0x28000;
    REG(0x9898)=cmd; REG(0x989c)=arg; REG(0x9884)=1;
    if (!wait_bit(0x9880,0x20000)) { return 0; }
    if (response) { *response=REG(0x989c); }
    return 1;
}
u32 sd_hw_init(SdHw *sd, u8 *dma)
{
    u32 r, rca;
    unsigned retry, i;
    u8 wire[16];
    sd->dma=dma; sd->capacity=0; sd->ready=0;
    REG(0x9888)=120; /* Development divider, not board timing calibration. */
    if (!command(0,0,0) || !command(8,0x1aa,&r) || r!=0x1aa) { return 0; }
    for (retry=0; retry<100; retry++) {
        if (!command(55,0,0) || !command(41,0x40ff8000,&r)) { return 0; }
        if (r&0x80000000u) { break; }
    }
    if (retry==100 || !(r&0x40000000u)) { return 0; } /* SDHC only */
    REG(0x988c)=(u32)dma;
    if (!command(2,0,0) || !command(3,0,&rca)) { return 0; }
    if (!command(9,rca,0)) { return 0; }
    for (i=0; i<16; i++) { wire[i]=dma[(i&~3u)+3-(i&3)]; }
    if ((wire[0]>>6)!=1) { return 0; }
    if (!command(7,rca,0)) { return 0; }
    /* Vendor arithmetic, kept verbatim for fidelity: the CSD csize field is in
     * 512 KiB units, and the vendor shifts by 10 rather than 19. The result is
     * numerically the sector count and stays inside u32 for every SDHC card. */
    sd->capacity=(((((u32)wire[7]&63)<<16)|((u32)wire[8]<<8)|wire[9])+1u)<<10;
    sd->ready=1;
    return sd->capacity;
}
static int sector_io(SdHw *sd, u32 lba, u8 *out, const u8 *in)
{
    unsigned i;
    if (!sd->ready) { return 0; }
    if (in) { for (i=0; i<SD_HW_SECTOR; i++) { sd->dma[i]=in[i]; } }
    REG(0x9890)=(u32)sd->dma; REG(0x9894)=SD_HW_SECTOR;
    REG(0x9884)=0x8000;
    if (!command(in ? 24 : 17,lba,0)) { return 0; }
    REG(0x9884)=0x10;
    if (!wait_bit(0x9880,0x8000) || (REG(0x9880)&0x10000)) { return 0; }
    if (out) { for (i=0; i<SD_HW_SECTOR; i++) { out[i]=sd->dma[i]; } }
    return 1;
}
int sd_hw_read(SdHw *sd, u32 lba, u8 *out) { return sector_io(sd,lba,out,0); }
int sd_hw_write(SdHw *sd, u32 lba, const u8 *in) { return sector_io(sd,lba,0,in); }
