/* SPDX-License-Identifier: MIT
 * Experimental original-KC02 first application stage, NOT flash-qualified.
 * Factory clocks/SDRAM retained; no LCD, sensor, SD/NOR writes or Luma launch.
 */
#include "monitor.h"
#include "ax_usb.h"
#include "recover.h"
#include "sd_hw.h"
#ifndef USB_VID
#define USB_VID 0
#define USB_PID 0
#endif
#ifndef BRINGUP_SELFTEST
#define BRINGUP_SELFTEST 0
#endif
#if BRINGUP_SELFTEST && (USB_VID || USB_PID)
#error Self-test must not attach USB or enable debug
#endif
#define REG(n) (*(volatile __sfr u32 *)(n))
static Monitor monitor;
static UsbDebug usb;
static AxUsb controller;
static unsigned key_phase, key_stable, key_previous;
static u8 sd_dma[512] __attribute__((aligned(32)));
static SdHw sd_card;
static int card_read(u32 lba, u8 *out) { return sd_hw_read(&sd_card,lba,out); }
/* Native observation points, not host-injected application state. */
volatile u32 bringup_probe[8] __attribute__((section(".probe.data")))={0};
extern void bringup_checkpoint(void);
extern u8 __application_stack[];
static void delay(unsigned n) { while (n--) { __asm__ volatile("l.nop"); } }
static u32 read_reg(void *ctx, unsigned reg) { (void)ctx; return REG(reg); }
static void write_reg(void *ctx, unsigned reg, u32 value) { (void)ctx; REG(reg)=value; }
static u32 dma_address(void *ctx, u8 *p) { (void)ctx; return (u32)p; }
static int memory_read(void *ctx, u32 a, u8 *out, unsigned n)
{
    volatile const u8 *p=(volatile const u8 *)a;
    unsigned i; (void)ctx;
    for (i=0; i<n; i++) { out[i]=p[i]; } return 1;
}
static int memory_write(void *ctx, u32 a, const u8 *data, unsigned n)
{
    volatile u8 *p=(volatile u8 *)a;
    unsigned i; (void)ctx;
    for (i=0; i<n; i++) { p[i]=data[i]; } return 1;
}
static void revoke(void *ctx)
{
    monitor_revoke(ctx); key_phase=key_stable=key_previous=0;
}
static void usb_phy(void)
{
    /* Register facts from SDK ax32xx_usb.c usb_phyreset_as_typeb. These delays
     * deliberately over-wait by loop count; silicon timing remains untested. */
    REG(0x8840)&=~((1u<<28)|(1u<<29)); /* USB20 + ADC active-low clock gates. */
    REG(0x9840)=0x21307400;
    REG(0x8850)&=~(1u<<16); delay(10000);
    REG(0x8848)=(REG(0x8848)&~((1u<<14)|(1u<<15)))|(1u<<16)|(1u<<14);
    REG(0x8850)|=1u<<16;
    REG(0x9840)=(REG(0x9840)&~((1u<<17)|(15u<<22)|(1u<<11)|(1u<<10)|(3u<<20)))|
                 (1u<<16)|(1u<<12)|(1u<<28)|(1u<<7);
    delay(10000); REG(0x9840)|=1u<<6; delay(1000000);
    REG(0x9840)|=1; delay(10000); REG(0x9840)|=4; delay(100000);
}
static void key_init(void)
{
    /* Original KC02 ADC3 is PB1, not the Z-12's extra lens key. */
    REG(0x8044)|=2; REG(0x8104)&=~2u; REG(0x8384)&=~2u;
    REG(0x9174)=127;
}
static unsigned key(void)
{
    unsigned timeout=10000, sample;
    REG(0x9170)=0x93;
    while ((REG(0x9170)&0x40) && --timeout) { }
    if (!timeout) { return 2; } /* Invalid sample cannot authorize. */
    sample=REG(0x9178)&1023;
    if (sample>990) { return 0; }
    return sample>=366 && sample<=406 ? 1 : 2;
}
static void authorize(void)
{
    unsigned k;
    if (!usb.configured || usb.fault || monitor.rsp.enabled) { return; }
    k=key();
    if (k!=key_previous) { key_previous=k; key_stable=0; }
    if (key_stable<32) { key_stable++; return; }
    /* Require a fresh release -> OK press -> release after configuration.
     * This is sample-based debounce, not a calibrated millisecond hold. */
    if (k==2) { key_phase=0; }
    else if (!key_phase && !k) { key_phase=1; }
    else if (key_phase==1 && k==1) { key_phase=2; }
    else if (key_phase==2 && !k) { monitor_authorize(&monitor); key_phase=0; }
}
u32 *bringup_service(u32 *frame)
{
    unsigned i;
    monitor_enter(&monitor,frame);
    bringup_probe[3]++;
    if (BRINGUP_SELFTEST) {
        for (i=2; i<32; i++) { if (monitor.regs[i]!=frame[i]) { bringup_probe[7]=1; } }
        monitor.resumed=1;
    } else {
        while (!monitor.resumed || ((USB_VID && USB_PID) && !ax_usb_idle(&controller))) {
            if (USB_VID && USB_PID) { ax_usb_poll(&controller); authorize(); }
            else { delay(10000); }
        }
    }
    for (i=0; i<RSP_REGS; i++) { frame[i]=monitor.regs[i]; }
    monitor.stopped=0;
    bringup_probe[4]++;
    return frame;
}
__attribute__((section(".probe.text"),noreturn)) void bringup_workload(void)
{
    for (;;) { bringup_probe[2]++; bringup_checkpoint(); }
}
void bringup_main(void)
{
    MonitorMemory memory={0,memory_read,memory_write};
    AxUsbIo io={0,read_reg,write_reg,dma_address};
    /* These writes only turn off the documented original-board supply/strobe. */
    REG(0x8004)&=~0x40u; REG(0x8000)&=~0x200u;
    REG(0x880c)=0x5500; /* SDK watchdog-disable key; bring-up only. */
    bringup_probe[0]=0x4c554d41;
    bringup_probe[1]=1; /* Recovery entry precedes any workload. */
    /* Recovery gate first, and read-only. It finds and validates a
     * stock-format candidate but cannot install one: there is no writer, and
     * a candidate is not trusted until the boot prefix can be verified. */
    {
        static RecoverReport recovery;
        u32 capacity=sd_hw_init(&sd_card,&sd_dma[0]);
        RecoverResult code=capacity ? recover_check(card_read,capacity,&recovery)
                                    : RECOVER_NO_CARD;
        bringup_probe[5]=code;
        bringup_probe[6]=recovery.capacity;
        bringup_probe[7]=recovery.info.crc;
    }
    bringup_probe[1]=2;
    monitor_init(&monitor,&usb,&memory);
    usb_debug_init(&usb,USB_VID,USB_PID,&monitor,monitor_receive,revoke);
    if (USB_VID && USB_PID) { usb_phy(); key_init(); ax_usb_init(&controller,&usb,&io); }
    bringup_probe[1]=3; /* Diagnostic monitor only; no application boot path. */
    __asm__ volatile("l.movhi r1,hi(__application_stack)\n\t"
                     "l.ori r1,r1,lo(__application_stack)\n\t"
                     "l.movhi r2,hi(bringup_workload)\n\t"
                     "l.ori r2,r2,lo(bringup_workload)\n\t"
                     "l.jr r2" : : : "memory");
    __builtin_unreachable();
}
