/* SPDX-License-Identifier: MIT
 * Host-side register/DMA contract model. NOT a physical USB test.
 */
#include <string.h>
#include "monitor.h"
#include "ax_usb.h"
static Monitor monitor;
static UsbDebug usb;
static AxUsb controller;
static u8 scratch[4096];
static u32 regs[16384], csr0, txcsr, rxcsr, count0, count1;
static unsigned reads, writes;
static int read_memory(void *ctx, u32 address, u8 *out, unsigned n)
{
    unsigned i; (void)ctx; reads++;
    for (i=0; i<n; i++) { out[i]=address+i>=MONITOR_SCRATCH ? scratch[address+i-MONITOR_SCRATCH] : (u8)(address+i); }
    return 1;
}
static int write_memory(void *ctx, u32 address, const u8 *data, unsigned n)
{
    (void)ctx; writes++; memcpy(scratch+address-MONITOR_SCRATCH,data,n); return 1;
}
static u32 rd(void *ctx, unsigned r)
{
    u32 v; (void)ctx;
    if (r==AX_USB_CSR0) { return regs[AX_USB_INDEX/4] ? txcsr : csr0; }
    if (r==AX_USB_RXCSR) { return rxcsr; }
    if (r==AX_USB_COUNT_L) { return (regs[AX_USB_INDEX/4] ? count1 : count0)&255; }
    if (r==AX_USB_COUNT_H) { return (regs[AX_USB_INDEX/4] ? count1 : count0)>>8; }
    v=regs[r/4];
    if (r==AX_USB_INTR || r==AX_USB_INTR_TX || r==AX_USB_INTR_RX) { regs[r/4]=0; }
    return v;
}
static void wr(void *ctx, unsigned r, u32 v)
{
    (void)ctx;
    if (r==AX_USB_CSR0 && !regs[AX_USB_INDEX/4]) {
        if (v&0x40) { csr0&=~1u; }
        if (v&0x80) { csr0&=~0x10u; }
        if (v&0x20) { csr0|=4; }
        if (v&2) { csr0|=2; }
        if (!v) { csr0=0; }
    } else if (r==AX_USB_TXCSR) {
        if (v&8) { txcsr=0; } else { txcsr=v; }
    } else if (r==AX_USB_RXCSR) {
        if (v&0x10) { rxcsr=0; } else { rxcsr=v; }
    } else { regs[r/4]=v; }
}
static u32 dma(void *ctx, u8 *p)
{
    (void)ctx;
    return p==controller.ep0 ? 1 : p==controller.rx ? 2 : 3;
}
void bringup_h_init(void)
{
    MonitorMemory memory={0,read_memory,write_memory};
    AxUsbIo io={0,rd,wr,dma};
    u32 frame[35]; unsigned i;
    memset(regs,0,sizeof(regs)); memset(scratch,0,sizeof(scratch));
    csr0=txcsr=rxcsr=count0=count1=reads=writes=0;
    monitor_init(&monitor,&usb,&memory);
    usb_debug_init(&usb,0x1234,0x5678,&monitor,monitor_receive,monitor_revoke);
    ax_usb_init(&controller,&usb,&io);
    for (i=0; i<35; i++) { frame[i]=0x1200+i; }
    frame[0]=0; frame[1]=0x027b0000; frame[32]=0x0200200c; frame[33]=0x02002010; frame[34]=0x8001;
    monitor_enter(&monitor,frame);
}
int bringup_h_setup(const u8 s[8], u8 out[64]) { return usb_debug_setup(&usb,s,out); }
int bringup_h_ack(void) { return usb_debug_status_complete(&usb); }
void bringup_h_authorize(void) { monitor_authorize(&monitor); }
unsigned bringup_h_state(unsigned n)
{
    switch(n) {
    case 0:return usb.address;
    case 1:return usb.configured;
    case 2:return monitor.rsp.enabled;
    case 3:return monitor.resumed;
    case 4:return usb.fault;
    case 5:return reads;
    case 6:return writes;
    case 7:return usb.halt_in;
    case 8:return usb.halt_out;
    case 9:return regs[AX_USB_ADDRESS/4];
    case 10:return controller.state;
    default:return 0;
    }
}
int bringup_h_feed(const u8 *p, unsigned n) { return usb_debug_receive(&usb,p,n); }
int bringup_h_queue(const u8 *p, unsigned n) { return usb_debug_send(&usb,p,n); }
unsigned bringup_h_take(u8 *out)
{
    unsigned n,total=0;
    while ((n=usb_debug_peek(&usb,out+total))!=0) { total+=n; usb_debug_sent(&usb,n); }
    return total;
}
void bringup_h_reset(void) { usb_debug_reset(&usb); }
void bringup_h_ax_setup(const u8 s[8])
{
    memcpy(controller.ep0,s,8); csr0=1; count0=8; regs[AX_USB_INTR_TX/4]|=1; ax_usb_poll(&controller);
}
void bringup_h_ax_ack(void)
{
    csr0&=~2u; regs[AX_USB_INTR_TX/4]|=1; ax_usb_poll(&controller);
}
void bringup_h_ax_status_out(void)
{
    csr0|=1; count0=0; ax_usb_poll(&controller);
}
void bringup_h_ax_setup_end(void)
{
    csr0|=0x10; count0=0; ax_usb_poll(&controller);
}
void bringup_h_ax_event(unsigned event)
{
    regs[AX_USB_INTR/4]|=event; ax_usb_poll(&controller);
}
void bringup_h_ax_feed(const u8 *p, unsigned n)
{
    if (n<=64) { memcpy(controller.rx,p,n); }
    count1=n; rxcsr|=1; ax_usb_poll(&controller);
}
unsigned bringup_h_ax_take(u8 *out)
{
    unsigned n=controller.transmitting;
    if (n) {
        memcpy(out,controller.tx,n); txcsr&=~1u;
        regs[AX_USB_INTR_TX/4]|=2; ax_usb_poll(&controller);
    }
    return n;
}

/* Compile-time recovery-result values, so the Python tests assert the real
 * enum order instead of restating it. */
#include "recover.h"
unsigned bringup_h_recover_code(unsigned n)
{
    static const unsigned codes[]={RECOVER_OK,RECOVER_NO_CARD,RECOVER_NO_FILESYSTEM,
        RECOVER_NO_CANDIDATE,RECOVER_CANDIDATE_IO,RECOVER_CANDIDATE_INVALID,
        RECOVER_PREFIX_UNAVAILABLE};
    return n<sizeof(codes)/sizeof(codes[0]) ? codes[n] : ~0u;
}
