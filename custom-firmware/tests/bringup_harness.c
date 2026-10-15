/* SPDX-License-Identifier: MIT
 * Host-side contract model for the debug monitor and the AX3292 USB registers.
 * This is NOT a physical USB test and NOT the QEMU run.
 *
 * Memory is a flat byte array so a trap word can be written, read back and
 * restored exactly as on target. `code_sync` is recorded rather than emulated:
 * what matters on the host is that the monitor calls it, because on silicon
 * that call is what makes a trap word visible to instruction fetch.
 */
#include <string.h>
#include "monitor.h"
#include "ax_usb.h"
#include "msc_medium.h"
#define H_CODE_BASE 0x02000000u
#define H_CODE_SIZE 0x10000u
#define H_SCRIPT_MAX 8
#define H_BODY_MAX 40
static Monitor monitor;
static UsbDevice usb;
static AxUsb controller;
static u8 code[H_CODE_SIZE];
static u8 scratch[4096];
static u32 regs[16384], csr0, txcsr, rxcsr, count0, count1;
static unsigned reads, writes, code_syncs;
static u8 wire[256];
static unsigned wire_size;
static char bodies[H_SCRIPT_MAX][H_BODY_MAX];
static unsigned body_count, body_at;
static int release;
/* --- memory --------------------------------------------------------------- */
static int in_code(u32 address, unsigned n)
{
    return address>=H_CODE_BASE && address<=H_CODE_BASE+H_CODE_SIZE && n<=H_CODE_BASE+H_CODE_SIZE-address;
}
static int in_scratch(u32 address, unsigned n)
{
    return address>=MON_SCRATCH && address<=MON_SCRATCH+MON_SCRATCH_SIZE &&
           n<=MON_SCRATCH+MON_SCRATCH_SIZE-address;
}
static int read_memory(void *ctx, u32 address, u8 *out, unsigned n)
{
    unsigned i; (void)ctx; reads++;
    for (i=0; i<n; i++) {
        u32 at=address+i;
        if (in_code(at,1)) { out[i]=code[at-H_CODE_BASE]; }
        else if (in_scratch(at,1)) { out[i]=scratch[at-MON_SCRATCH]; }
        else { out[i]=(u8)at; }
    }
    return 1;
}
static int write_memory(void *ctx, u32 address, const u8 *data, unsigned n)
{
    (void)ctx; writes++;
    if (in_code(address,n)) { memcpy(code+address-H_CODE_BASE,data,n); return 1; }
    if (in_scratch(address,n)) { memcpy(scratch+address-MON_SCRATCH,data,n); return 1; }
    return 0;
}
static void code_sync(void *ctx, u32 address, unsigned size)
{
    (void)ctx; (void)address; (void)size; code_syncs++;
}
/* --- AX3292 USB register/DMA contract ------------------------------------ */
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
/* The DMA address registers must be followed, not approximated: the controller
 * reads and writes through whatever address was last written, so a token that
 * does not resolve back to the same bytes silently tests the wrong buffer.
 * These offsets are stable because the buffers live inside AxUsb. */
static u32 dma(void *ctx, u8 *p)
{
    const u8 *base=(const u8 *)&controller;
    (void)ctx;
    if (p>=(const u8 *)&controller && p<base+sizeof(controller)) {
        return (u32)(p-base);
    }
    return 0xffffffffu;
}
static const u8 *dma_ptr(u32 token)
{
    if (token==0xffffffffu) { return 0; }
    if (token>=sizeof(controller)) { return 0; }
    return (const u8 *)&controller+token;
}
/* --- RSP script ----------------------------------------------------------- */
static const char *hex_digits="0123456789abcdef";
static unsigned body_len(const char *s) { unsigned n=0; while (s[n]) { n++; } return n; }
static void feed_body(void)
{
    u8 packet[64];
    unsigned n=0, sum=0, i=0, chunk;
    const char *body=bodies[body_at];
    packet[n++]='$';
    while (body[i]) { packet[n++]=(u8)body[i]; sum=(sum+(u8)body[i])&255; i++; }
    packet[n++]='#';
    packet[n++]=hex_digits[sum>>4];
    packet[n++]=hex_digits[sum&15];
    usb.queued=usb.consumed=0;
    usb.fault=0;
    wire_size=0;
    monitor_receive(&monitor,packet,n);
    while ((chunk=usb_device_peek(&usb,wire+wire_size))!=0) {
        wire_size+=chunk;
        usb_device_sent(&usb,chunk);
        if (wire_size+64>sizeof(wire)) { break; }
    }
    monitor.link_failed=0;
}
static int harness_probe(void *ctx, Monitor *m)
{
    (void)ctx; (void)m;
    if (release) { return 1; }
    if (body_at>=body_count) { release=1; return 1; }
    feed_body();
    body_at++;
    return 0;
}
/* --- entry points --------------------------------------------------------- */
static void make_frame(u32 *frame, const u32 *gprs, u32 pc, u32 sr)
{
    unsigned i;
    for (i=0; i<RSP_REGS; i++) { frame[i]=0; }
    if (gprs) { for (i=0; i<32; i++) { frame[i]=gprs[i]; } }
    frame[1]=0x027b0000;      /* application stack */
    frame[32]=pc-4;           /* derived, as the exception entry does */
    frame[33]=pc;
    frame[34]=sr;
    frame[35]=0;
}
void bringup_h_init(void)
{
    MonitorMemory memory={0,read_memory,write_memory,code_sync};
    MonitorRegions regions={H_CODE_BASE, H_CODE_BASE+H_CODE_SIZE-4,
                            H_CODE_BASE, MON_SCRATCH+MON_SCRATCH_SIZE-4,
                            MON_SCRATCH, MON_SCRATCH+MON_SCRATCH_SIZE-4};
    AxUsbIo io={0,rd,wr,dma};
    u32 frame[RSP_REGS+1];
    unsigned i;
    memset(regs,0,sizeof(regs));
    memset(scratch,0,sizeof(scratch));
    memset(code,0,sizeof(code));
    for (i=0; i<H_CODE_SIZE; i++) { code[i]=(u8)i; }
    csr0=txcsr=rxcsr=count0=count1=0;
    reads=writes=code_syncs=0;
    body_count=body_at=0; release=0; wire_size=0;
    monitor_init(&monitor,&usb,&memory,&regions);
    monitor.probe=harness_probe;
    usb_device_init(&usb,0x1234,0x5678,&monitor,monitor_receive,monitor_revoke);
    ax_usb_init(&controller,&usb,&io);
    make_frame(frame,0,0x02002010,0x8001);
    frame[33]=0x027b0000;
    frame[34]=0x8001;
    (void)frame;
}
/* Scripted requests are handed over while the target is stopped, exactly as
 * the guest self-test does over the real engine. */
void bringup_h_script(unsigned index, const char *body)
{
    unsigned n=body_len(body);
    if (index>=H_SCRIPT_MAX || n>=H_BODY_MAX) { return; }
    memcpy(bodies[index],body,n+1);
    if (index+1>body_count) { body_count=index+1; }
}
void bringup_h_script_clear(void) { body_count=body_at=0; release=0; }
void bringup_h_checkpoint(const u32 *gprs)
{
    u32 frame[RSP_REGS+1];
    make_frame(frame,gprs,0x02002010,0x8001);
    monitor_checkpoint(&monitor,frame);
}
void bringup_h_trap(const u32 *gprs, u32 pc, u32 sr)
{
    u32 frame[RSP_REGS+1];
    make_frame(frame,gprs,pc,sr);
    frame[35]=14;             /* EXCP_TRAP */
    monitor_exception(&monitor,frame);
}
void bringup_h_authorize(void) { monitor_authorize(&monitor); }
void bringup_h_reset(void) { usb_device_reset(&usb); }
int bringup_h_setup(const u8 s[8], u8 out[64])
{
    const u8 *reply;
    unsigned size=0, i;
    int n=usb_device_setup(&usb,s);
    if (n<=0) { return n; }
    reply=usb_device_response(&usb,&size);
    if ((unsigned)n>size) { return -1; }
    for (i=0; i<(unsigned)n; i++) { out[i]=reply[i]; }
    return n;
}
int bringup_h_ack(void) { return usb_device_status_complete(&usb); }
int bringup_h_feed(const u8 *p, unsigned n) { return usb_device_receive(&usb,p,n); }
int bringup_h_queue(const u8 *p, unsigned n) { return usb_device_send(&usb,p,n); }
unsigned bringup_h_take(u8 *out)
{
    unsigned n,total=0;
    while ((n=usb_device_peek(&usb,out+total))!=0) { total+=n; usb_device_sent(&usb,n); }
    return total;
}
unsigned bringup_h_wire(u8 *out, unsigned cap)
{
    unsigned n=wire_size<cap ? wire_size : cap;
    memcpy(out,wire,n);
    return wire_size;
}
void put_word(u8 *out, u32 value)
{
    out[0]=(u8)value; out[1]=(u8)(value>>8); out[2]=(u8)(value>>16); out[3]=(u8)(value>>24);
}
u32 get_word(const u8 *in)
{
    return (u32)in[0] | ((u32)in[1]<<8) | ((u32)in[2]<<16) | ((u32)in[3]<<24);
}
void bringup_h_set_code(u32 address, u32 value)
{
    if (in_code(address,4)) { put_word(code+address-H_CODE_BASE,value); }
}
u32 bringup_h_get_code(u32 address)
{
    return in_code(address,4) ? get_word(code+address-H_CODE_BASE) : 0;
}
unsigned bringup_h_code_syncs(void) { return code_syncs; }
unsigned bringup_h_stop_reason(void) { return monitor.stop_reason; }
unsigned bringup_h_reg(unsigned index) { return index<RSP_REGS ? monitor.regs[index] : 0; }
int bringup_h_patched(void) { return monitor_code_patched(&monitor); }
unsigned bringup_h_reads(void) { return reads; }
unsigned bringup_h_writes(void) { return writes; }
/* --- mass storage over the real endpoints --------------------------------- */
#define H_CARD_BLOCKS 16
static u8 h_card[H_CARD_BLOCKS*512];
static Fat h_fat;
static MscSd h_sd;
static Msc h_msc;
static int card_read_block(void *ctx, u32 lba, u8 *out)
{
    (void)ctx;
    if (lba>=H_CARD_BLOCKS) { return 0; }
    memcpy(out,h_card+lba*512,512);
    return 1;
}
static int card_write_block(void *ctx, u32 lba, const u8 *in)
{
    (void)ctx;
    if (lba>=H_CARD_BLOCKS) { return 0; }
    memcpy(h_card+lba*512,in,512);
    return 1;
}
static void bot_reset(void *ctx)
{
    (void)ctx;
    msc_reset(&h_msc);          /* as a Bulk-Only Mass Storage Reset must */
}
/* Binds the engine to the storage endpoint and to an in-memory card, under the
 * real ownership gate. The LUN starts unexported, as it does on target. */
void bringup_h_msc_init(void)
{
    MscMedium medium;
    MscLink link;
    unsigned i;
    for (i=0; i<sizeof(h_card); i++) { h_card[i]=(u8)(i*5+1); }
    h_fat=(Fat){0};
    h_fat.mounted=1;
    h_fat.owner=SD_LOCAL;
    h_fat.writable=1;
    msc_sd_init(&h_sd,&h_fat,H_CARD_BLOCKS,card_read_block,card_write_block,0);
    msc_sd_medium(&h_sd,&medium);
    link=(MscLink){&controller,USB_DEVICE_PACKET,ax_usb_msc_send,ax_usb_msc_receive,
                   ax_usb_msc_stall,ax_usb_msc_reset};
    msc_init(&h_msc,&medium,&link);
    usb.bot_reset=bot_reset;
    ax_usb_msc_reset(&controller);
}
int bringup_h_msc_export(int io_idle)
{
    int ok=msc_sd_export(&h_sd,io_idle);
    MscMedium medium;
    msc_sd_medium(&h_sd,&medium);
    h_msc.medium=medium;
    return ok;
}
int bringup_h_msc_reclaim(int io_idle)
{
    int ok=msc_sd_reclaim(&h_sd,io_idle);
    MscMedium medium;
    msc_sd_medium(&h_sd,&medium);
    h_msc.medium=medium;
    return ok;
}
unsigned bringup_h_msc_info(unsigned n)
{
    switch (n) {
    case 0: return h_sd.exports;
    case 1: return h_sd.reclaims;
    case 2: return h_sd.refused;
    case 3: return msc_sd_host_owns(&h_sd);
    case 4: return h_fat.owner;
    case 5: return h_fat.mounted;
    case 6: return h_msc.medium.blocks;
    case 7: return h_msc.commands;
    case 8: return h_msc.errors;
    case 9: return h_msc.stalls;
    case 10: return controller.msc.tx_used;
    case 11: return controller.msc.tx_sent;
    case 12: return controller.msc.transmitting;
    case 13: return controller.msc.rx_used;
    case 14: return controller.msc.halt_in;
    case 15: return controller.msc.fault;
    case 16: return h_msc.phase;
    case 17: return h_msc.expect;
    case 18: return h_msc.data_total;
    case 19: return h_msc.transferred;
    default: return 0;
    }
}
/* The host hands over one CBW, the engine runs the whole command, and the test
 * reads the reply from the storage endpoint. */
int bringup_h_msc_command(void)
{
    u8 cbw[MSC_CBW_SIZE];
    unsigned guard, n;
    n=ax_usb_msc_receive(&controller,cbw,sizeof(cbw));
    if (n!=MSC_CBW_SIZE) { return -2; }
    if (msc_cbw(&h_msc,cbw,MSC_CBW_SIZE)<0) { return -1; }
    for (guard=0; guard<8192 && !msc_idle(&h_msc); guard++) {
        if (msc_poll(&h_msc)<0) { break; }
    }
    return h_msc.csw[12];
}
unsigned bringup_h_msc_sense(void)
{
    return ((unsigned)h_msc.sense_key<<8)|h_msc.sense_asc;
}
unsigned bringup_h_card_byte(unsigned offset)
{
    return offset<sizeof(h_card) ? h_card[offset] : 0xffffffffu;
}
void bringup_h_ax_msc_endpoint_reset(void)
{
    ax_usb_msc_reset(&controller);
}
/* Mass-storage endpoint, kept separate from the debug endpoint so a test can
 * drive a BOT session without disturbing the monitor's traffic. */
void bringup_h_ax_msc_feed(const u8 *p, unsigned n)
{
    if (n<=64) { memcpy(controller.msc.packet_rx,p,n); }
    count1=n; rxcsr|=1;
    regs[AX_USB_INTR_RX/4]|=1u<<1;      /* mass-storage OUT packet */
    ax_usb_poll(&controller);
}
/* The host reading bulk IN: a poll moves staged bytes into the endpoint packet
 * buffer, and the test completes the transfer the way the controller would. */
unsigned bringup_h_ax_msc_take(u8 *out, unsigned cap)
{
    unsigned n=0;
    for (;;) {
        const u8 *src;
        unsigned got;
        ax_usb_poll(&controller);
        got=controller.msc.transmitting;
        if (!got || n+got>cap) { break; }
        src=dma_ptr(regs[AX_USB_EP_TX_REG(1)/4]);
        if (!src) { break; }        /* no address was armed: nothing to read */
        memcpy(out+n,src,got);
        n+=got;
        txcsr&=~1u;
        regs[AX_USB_INTR_TX/4]|=1u<<1;
        ax_usb_poll(&controller);
    }
    return n;
}
unsigned bringup_h_ax_msc_stalled(unsigned in)
{
    return in ? controller.msc.stalled_in : controller.msc.stalled_out;
}
void bringup_h_ax_msc_clear(void)
{
    ax_usb_msc_reset(&controller);
}
unsigned bringup_h_state(unsigned n)
{
    switch(n) {
    case 0:return usb.address;
    case 1:return usb.configured;
    case 2:return monitor.rsp.enabled;
    case 3:return monitor.leave;
    case 4:return usb.fault;
    case 5:return reads;
    case 6:return writes;
    case 7:return usb_device_halted(&usb,USB_DEVICE_DEBUG_IN);
    case 8:return usb_device_halted(&usb,USB_DEVICE_DEBUG_OUT);
    case 9:return regs[AX_USB_ADDRESS/4];
    case 10:return controller.state;
    default:return 0;
    }
}
/* --- AX USB controller contract (unchanged) ------------------------------ */
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
    if (n<=64) { memcpy(controller.debug.packet_rx,p,n); }
    count1=n; rxcsr|=1;
    regs[AX_USB_INTR_RX/4]|=1u<<2;      /* debug OUT packet */
    ax_usb_poll(&controller);
}
unsigned bringup_h_ax_take(u8 *out)
{
    unsigned n;
    /* The reply is queued by the receive path; a poll is what moves it into the
     * endpoint buffer, exactly as the target's loop does. */
    ax_usb_poll(&controller);
    n=controller.debug.transmitting;
    if (n) {
        memcpy(out,controller.debug.packet_tx,n);
        txcsr&=~1u;                     /* TXPKTRDY clear: transfer complete */
        regs[AX_USB_INTR_TX/4]|=1u<<2;  /* debug IN transfer complete */
        ax_usb_poll(&controller);
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
