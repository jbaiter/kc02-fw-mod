/* SPDX-License-Identifier: MIT
 * Experimental original-KC02 first application stage, NOT flash-qualified.
 * Factory clocks/SDRAM retained; no LCD, sensor, SD/NOR writes or Luma launch.
 */
#include "monitor.h"
#include "ax_usb.h"
#include "recover.h"
#include "sd_hw.h"
#include "msc_medium.h"
#include "update_sd.h"
#include "webusb.h"
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
#define PROBE_WORDS 32
static Monitor monitor;
static UsbDevice usb;
static AxUsb controller;
static unsigned key_phase, key_stable, key_previous;
static u8 sd_dma[512] __attribute__((aligned(32)));
static SdHw sd_card;
static Fat volume;
static MscSd lun;
static Msc storage;
/* The gate mounts read-only, so there is no callback that writes through the
 * filesystem. Writing goes to the raw LUN below, and only after the card has
 * been handed to the host. */
static int card_read(u32 lba, u8 *out) { return sd_hw_read(&sd_card,lba,out); }
static int lun_read(void *ctx, u32 lba, u8 *out)
{
    (void)ctx;
    return sd_hw_read(&sd_card,lba,out);
}
static int lun_write(void *ctx, u32 lba, const u8 *in)
{
    (void)ctx;
    return sd_hw_write(&sd_card,lba,in);
}
/* Native observation points, not host-injected application state. */
volatile u32 bringup_probe[PROBE_WORDS] __attribute__((section(".probe.data")))={0};
extern void bringup_checkpoint(void);
extern u8 __application_stack[];
#if BRINGUP_SELFTEST
void bringup_selftest_start(Monitor *);
void bringup_step_caller(void);
#endif
static WebUsb webusb;
static u32 capacity;
/* The board this stage runs on. Original KC02 only for now. */
static const u32 kind_bit=1;
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
/* A trap word has to reach instruction fetch, not just data load. This follows
 * the vendor's invalidate sequence (ax32xx_sysIcacheInit writes one line
 * address to SPR_ICAIR). The emulator backs ICAIR as an inert register, so this
 * path is written to the vendor contract and is NOT verified sufficient on
 * silicon. A wrong or missing flush shows up as a trap that never fires. */
static void code_sync(void *ctx, u32 address, unsigned size)
{
    u32 line=address & ~15u, last=(address+size-1) & ~15u;
    (void)ctx;
    for (; line<=last; line+=16) { __asm__ volatile("l.mtspr r0,%0,0x2008" : : "r"(line)); }
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
/* WebUSB adapters. The device layer knows nothing about the WebUSB command
 * numbering, so the mapping lives here. */
static unsigned webusb_command_of(void *ctx, unsigned request)
{
    (void)ctx;
    switch (request) {
    case WEBUSB_CMD_BEGIN: return WEBUSB_CMD_BEGIN;
    case WEBUSB_CMD_COMMIT: return WEBUSB_CMD_COMMIT;
    case WEBUSB_CMD_ABORT: return WEBUSB_CMD_ABORT;
    default: return 0;
    }
}
static unsigned webusb_status_of(void *ctx, unsigned request)
{
    (void)ctx;
    return request==WEBUSB_CMD_STATUS ? WEBUSB_CMD_STATUS : 0;
}
static void webusb_on_command(void *ctx, unsigned command)
{
    (void)ctx;
    webusb_command(&webusb,command);
}
static int webusb_status_reply(void *ctx, u8 *out, unsigned size)
{
    (void)ctx;
    return webusb_status_read(&webusb,out,size);
}
/* Drains whatever the update endpoint has collected into the staging engine. */
static void webusb_service(void)
{
    u8 chunk[64];
    unsigned have;
    while ((have=ax_usb_update_available(&controller))!=0) {
        unsigned want=have>sizeof(chunk) ? sizeof(chunk) : have;
        int got=ax_usb_update_receive(&controller,chunk,want);
        if (got<=0) { break; }
        if (!webusb_feed(&webusb,chunk,(unsigned)got)) {
            /* The container was refused: abort the staging write so nothing
             * half-written is left behind. */
            webusb_command(&webusb,WEBUSB_CMD_ABORT);
            bringup_probe[17]++;
            break;
        }
    }
}
static void poll_link(void *ctx)
{
    (void)ctx;
    if (USB_VID && USB_PID) {
        webusb_service();
        /* The BOT engine drives its own transfers, so it is advanced from the
         * same place the endpoints are serviced. */
        if (!msc_idle(&storage)) { msc_poll(&storage); }
        else if (ax_usb_msc_cbw_ready(&controller)) {
            u8 cbw[MSC_CBW_SIZE];
            unsigned n=ax_usb_msc_receive(&controller,cbw,sizeof(cbw));
            if (n==MSC_CBW_SIZE) { msc_cbw(&storage,cbw,n); }
        }
        ax_usb_poll(&controller);
        authorize();
    }
    else { delay(2000); }
}
#if BRINGUP_SELFTEST
UsbDevice *bringup_selftest_usb(void) { return &usb; }
#endif
static void bot_reset(void *ctx)
{
    (void)ctx;
    msc_reset(&storage);
}
/* The LUN is exported only when the local filesystem has released the card, so
 * a PC never shares a FAT with the camera. Until then the host sees NOT READY,
 * not an empty disk. */
static void lun_export(void)
{
    MscMedium medium;
    if (msc_sd_export(&lun,1)) {
        msc_sd_medium(&lun,&medium);
        storage.medium=medium;
        bringup_probe[9]=1;
    }
}
static void lun_reclaim(void *ctx)
{
    MscMedium medium;
    (void)ctx;
    if (msc_sd_reclaim(&lun,1)) { bringup_probe[10]++; }
    msc_sd_medium(&lun,&medium);
    storage.medium=medium;
}
static void lun_configured(void *ctx)
{
    MscMedium medium;
    (void)ctx;
    lun_export();
    msc_sd_medium(&lun,&medium);
    storage.medium=medium;
}
u32 *bringup_service(u32 *frame)
{
    unsigned i;
    monitor_checkpoint(&monitor,frame);
    bringup_probe[3]++;
    for (i=0; i<RSP_REGS; i++) { if (monitor.regs[i]!=frame[i]) { bringup_probe[7]=1; } }
    return frame;
}
u32 *bringup_exception_service(u32 *frame)
{
    bringup_probe[8]++;
    monitor_exception(&monitor,frame);
    return frame;
}
__attribute__((section(".probe.text"),noreturn)) void bringup_workload(void)
{
#if BRINGUP_SELFTEST
    /* One checkpoint to let the self-test install its breakpoint, then run the
     * shaped instruction sequence so every step has a real target. */
    bringup_selftest_start(&monitor);
    bringup_probe[2]++;
    bringup_checkpoint();
    bringup_step_caller();
#endif
    for (;;) {
        bringup_probe[2]++;
        bringup_checkpoint();
    }
}
void bringup_main(void)
{
    MonitorMemory memory={0,memory_read,memory_write,code_sync};
    MonitorRegions regions={
        .code_low=0x02000000, .code_high=0x02000000+0xc8800-4,
        .ram_low=MON_RAM_LOW, .ram_high=MON_SCRATCH-4,
        .scratch_low=MON_SCRATCH, .scratch_high=MON_SCRATCH+MON_SCRATCH_SIZE-4
    };
    AxUsbIo io={0,read_reg,write_reg,dma_address};
    /* These writes only turn off the documented original-board supply/strobe. */
    REG(0x8004)&=~0x40u; REG(0x8000)&=~0x200u;
    REG(0x880c)=0x5500; /* SDK watchdog-disable key; bring-up only. */
    bringup_probe[0]=0x4c554d41;
    bringup_probe[1]=1; /* Recovery entry precedes any workload. */
    /* Recovery gate first, and read-only. It finds and validates a
     * stock-format candidate but cannot install one: there is no writer, and
     * a candidate is not trusted until the boot prefix can be verified. */
    capacity=sd_hw_init(&sd_card,&sd_dma[0]);
    {
        static RecoverReport recovery;
        RecoverResult code=capacity ? recover_check(&volume,card_read,capacity,&recovery)
                                    : RECOVER_NO_CARD;
        bringup_probe[5]=code;
        bringup_probe[6]=recovery.capacity;
        bringup_probe[15]=recovery.info.crc;
    }
    /* The field-update path: find FIRMWARE.IMG and validate it read-only. The
     * version and size land in the observation words so an operator can see
     * what is on the card. Nothing is written: there is still no NOR driver,
     * and the engine's confirmation and recovery gates have not been given. */
    {
        static UpdateSdReport candidate;
        UpdateSdResult found=kind_bit ? update_sd_check(&volume,kind_bit,&candidate)
                                      : UPDATE_SD_NO_CARD;
        bringup_probe[11]=found;
        bringup_probe[12]=candidate.info.version;
        bringup_probe[13]=candidate.info.bytes;
        bringup_probe[14]=candidate.validated;
    }
    /* The WebUSB update function. It stages an upload on the card and applies
     * it only on a separate command; firmware still needs a device-local
     * confirmation that nothing here can supply, and there is no NOR driver, so
     * an application would be refused after being validated. */
    {
        WebUsbConfig config;
        config.volume=&volume;
        config.nor=0;              /* No NOR driver yet, so nothing installs. */
        config.board_bit=kind_bit;
        config.locally_confirmed=0;
        config.recovery_verified=0;
        webusb_init(&webusb,&config);
    }
    bringup_probe[1]=2;
    /* Mass storage starts with the card owned locally, so a host that
     * enumerates before configuring sees NOT READY rather than a blank disk.
     * The LUN is handed over when the host configures the device. */
    {
        static const MscLink link={0,USB_DEVICE_PACKET,ax_usb_msc_send,ax_usb_msc_receive,
                                   ax_usb_msc_stall,ax_usb_msc_reset};
        MscMedium medium;
        MscLink bound=link;
        bound.context=&controller;
        msc_sd_init(&lun,&volume,capacity,lun_read,lun_write,0);
        msc_sd_medium(&lun,&medium);
        msc_init(&storage,&medium,&bound);
    }
    monitor_init(&monitor,&usb,&memory,&regions);
    monitor.poll=poll_link;
    usb_device_init(&usb,USB_VID,USB_PID,&monitor,monitor_receive,revoke);
    usb.bot_reset=bot_reset;
    usb.revoke=revoke;
    usb.bos=webusb_bos;
    usb.landing_url=webusb_url;
    usb.vendor_code=WEBUSB_VENDOR_CODE;
    usb.url_request=WEBUSB_GET_URL;
    usb.update_command=webusb_on_command;
    usb.update_command_of=webusb_command_of;
    usb.update_status=webusb_status_reply;
    usb.update_status_of=webusb_status_of;
    usb.bus_reset=lun_reclaim;
    usb.on_configured=lun_configured;
    if (USB_VID && USB_PID) {
        usb_phy(); key_init(); ax_usb_init(&controller,&usb,&io);
    }
    bringup_probe[1]=3; /* Diagnostic monitor only; no application boot path. */
    __asm__ volatile("l.movhi r1,hi(__application_stack)\n\t"
                     "l.ori r1,r1,lo(__application_stack)\n\t"
                     "l.movhi r2,hi(bringup_workload)\n\t"
                     "l.ori r2,r2,lo(bringup_workload)\n\t"
                     "l.jr r2" : : : "memory");
    __builtin_unreachable();
}
