/* SPDX-License-Identifier: MIT
 * QEMU-only self-test driver. Built only when BRINGUP_SELFTEST=1.
 *
 * It drives the real RSP engine, the real monitor, the real exception vectors
 * and the real step logic, and it checks the exact bytes the monitor puts on
 * the wire. Only the transport is stood in for: packets are handed over in the
 * guest, because the emulator does not model the USB controller.
 *
 * Results land in bringup_probe, which the QEMU run reads back over QMP.
 */
#include "monitor.h"
#include "or1k_decode.h"
extern volatile u32 bringup_probe[];
extern UsbDebug *bringup_selftest_usb(void);
extern void bringup_step_caller(void);
extern void bringup_step_target(void);
static const char hex[]="0123456789abcdef";

/* Observation words. 0..15 belong to the bring-up stage itself. */
enum {
    PROBE_WIRE_SIZE = 16,
    PROBE_WIRE_WORD,
    PROBE_FAILURES,
    PROBE_STAGE,
    PROBE_PC0 = 20,         /* seven stops: breakpoint, then six steps */
    PROBE_ORIGINAL = 27,
    PROBE_PATCHED,
    PROBE_REASON,
};
/* One bit per checked expectation, so a failure names itself. */
enum {
    FAIL_NOT_CHECKPOINT, FAIL_INSTALL, FAIL_STOP1_REASON, FAIL_STOP1_PC,
    FAIL_STEP1_PC, FAIL_STEP2_PC, FAIL_STEP3_PC, FAIL_BRANCH_TAKEN,
    FAIL_STEP5_PC, FAIL_STEP6_PC, FAIL_RETURN_PC, FAIL_TRAP_WORD,
    FAIL_REMOVE, FAIL_RESTORED, FAIL_DETACH, FAIL_PATCH_LEFT, FAIL_FRAME,
    FAIL_READ_BASE,
};
static Monitor *mon;
static UsbDebug *link;
static unsigned stage, failures, pc_count;
static u32 pc_seen[7];
static u32 original;
static u8 wire[128];
static unsigned wire_size;
static void hex8(char *out, u32 value)
{
    unsigned i;
    for (i=0; i<8; i++) { out[i]=hex[(value>>(28-4*i))&15]; }
}
static int same(const u8 *got, unsigned n, const char *want)
{
    unsigned i=0;
    while (want[i]) {
        if (i>=n || got[i]!=(u8)want[i]) { return 0; }
        i++;
    }
    return i==n;
}
static void build_ok(char *out)
{
    unsigned sum=(unsigned)'O'+(unsigned)'K';
    out[0]='+'; out[1]='$'; out[2]='O'; out[3]='K';
    out[4]='#'; out[5]=hex[(sum>>4)&15]; out[6]=hex[sum&15]; out[7]=0;
}
/* The RSP memory reply is the bytes in address order, so a little-endian word
 * appears byte-reversed on the wire. Expecting the numeric hex instead is a
 * classic way to conclude the target is broken when it is not. */
static void build_word(char *out, u32 value)
{
    unsigned sum=0, i;
    u8 bytes[4];
    bytes[0]=(u8)value; bytes[1]=(u8)(value>>8);
    bytes[2]=(u8)(value>>16); bytes[3]=(u8)(value>>24);
    out[0]='+'; out[1]='$';
    for (i=0; i<4; i++) {
        out[2+2*i]=hex[bytes[i]>>4];
        out[3+2*i]=hex[bytes[i]&15];
        sum+=(unsigned)out[2+2*i]+(unsigned)out[3+2*i];
    }
    out[10]='#'; out[11]=hex[(sum>>4)&15]; out[12]=hex[sum&15]; out[13]=0;
}
/* Z0/z0 are "Z0,<addr>,4"; the memory read is "m<addr>,4" with no separator. */
static void build_request(char *out, const char *op, u32 address, int separator)
{
    unsigned i=0, j=0;
    char encoded[9];
    hex8(encoded,address);
    while (op[j]) { out[i++]=op[j++]; }
    if (separator) { out[i++]=','; }
    for (j=0; j<8; j++) { out[i++]=encoded[j]; }
    out[i++]=',';
    out[i++]='4';
    out[i]=0;
}
/* Sends one request and keeps only what it produced. */
static void request(const char *body)
{
    u8 packet[64];
    unsigned n=0, sum=0, i=0, chunk;
    packet[n++]='$';
    while (body[i]) { packet[n++]=(u8)body[i]; sum=(sum+(u8)body[i])&255; i++; }
    packet[n++]='#';
    packet[n++]=hex[sum>>4];
    packet[n++]=hex[sum&15];
    link->queued=link->consumed=0;
    wire_size=0;
    monitor_receive(mon,packet,n);
    while ((chunk=usb_debug_peek(link,wire+wire_size))!=0) {
        wire_size+=chunk;
        usb_debug_sent(link,chunk);
        if (wire_size+64>sizeof(wire)) { break; }
    }
    /* A refused send revokes debug on purpose. That rule is covered elsewhere;
     * here it would only cascade into unrelated failures. */
    mon->link_failed=0;
    bringup_probe[PROBE_WIRE_SIZE]=wire_size;
    bringup_probe[PROBE_WIRE_WORD]=(wire_size>=4) ?
        ((u32)wire[0]|((u32)wire[1]<<8)|((u32)wire[2]<<16)|((u32)wire[3]<<24)) : 0;
}
static void require(int condition, unsigned bit)
{
    if (condition) { return; }
    failures|=1u<<bit;
    /* Keep the first failing exchange: its size and first four wire bytes are
     * what makes a failure diagnosable from the QMP read-back alone. */
    if (!bringup_probe[30]) {
        bringup_probe[30]=wire_size;
        bringup_probe[31]=(wire_size>=4) ?
            ((u32)wire[0]|((u32)wire[1]<<8)|((u32)wire[2]<<16)|((u32)wire[3]<<24)) : 0;
    }
}
static void request_ok(const char *body, unsigned bit)
{
    char want[16];
    build_ok(want);
    request(body);
    require(same(wire,wire_size,want),bit);
}
static void request_word(const char *body, u32 value, unsigned bit)
{
    char want[24];
    build_word(want,value);
    request(body);
    require(same(wire,wire_size,want),bit);
}
static void publish(void)
{
    unsigned i;
    bringup_probe[PROBE_FAILURES]=failures;
    bringup_probe[PROBE_STAGE]=stage;
    for (i=0; i<7; i++) { bringup_probe[PROBE_PC0+i]=pc_seen[i]; }
    bringup_probe[PROBE_ORIGINAL]=original;
    bringup_probe[PROBE_PATCHED]=monitor_code_patched(mon);
    bringup_probe[PROBE_REASON]=mon->stop_reason;
}
static int probe(void *context, Monitor *monitor);
void bringup_selftest_start(Monitor *monitor)
{
    mon=monitor;
    link=bringup_selftest_usb();
    /* A configured transport with a guest-side drain, so the monitor's own
     * send path runs exactly as it would over USB. */
    link->configured=1;
    link->fault=0;
    link->queued=link->consumed=0;
    mon->probe=probe;
    mon->probe_context=0;
    stage=0;
    failures=0;
    pc_count=0;
    monitor_authorize(mon);
    publish();
}
static int probe(void *context, Monitor *monitor)
{
    char body[32];
    u32 address=(u32)(void (*)(void))bringup_step_target;
    u32 caller=(u32)(void (*)(void))bringup_step_caller;
    (void)context;
    if (stage>=8) { return 1; }   /* script done: release without reporting */
    bringup_probe[PROBE_STAGE]=stage;
    if (stage==0) {
        require(monitor->stop_reason==MON_STOP_CHECKPOINT,FAIL_NOT_CHECKPOINT);
        original=*(volatile u32 *)address;
        /* The debugger's read path must agree with the target's own view
         * before any patch, so a later mismatch cannot be blamed on one. */
        build_request(body,"m",address,0);
        request_word(body,original,FAIL_READ_BASE);
        build_request(body,"Z0",address,1);
        request_ok(body,FAIL_INSTALL);
        request("c");
        require(same(wire,wire_size,"+"),FAIL_DETACH);
        stage=1;
        return 0;
    }
    if (pc_count<7) { pc_seen[pc_count]=monitor->regs[33]; }
    pc_count++;
    if (stage==1) {
        require(monitor->stop_reason==MON_STOP_BREAKPOINT,FAIL_STOP1_REASON);
        require(pc_seen[0]==address,FAIL_STOP1_PC);
    }
    if (stage==2) { require(pc_seen[1]==address+4,FAIL_STEP1_PC); }
    if (stage==3) { require(pc_seen[2]==address+8,FAIL_STEP2_PC); }
    if (stage==4) {
        /* The conditional branch is taken, so the next instruction is the
         * branch target and not the following address. A decoder that only
         * traps pc+4 lands at address+0x0c instead and this fails. */
        require(pc_seen[3]==address+0x10,FAIL_STEP3_PC);
        require(pc_seen[3]!=address+0x0c,FAIL_BRANCH_TAKEN);
    }
    if (stage==5) { require(pc_seen[4]==address+0x14,FAIL_STEP5_PC); }
    if (stage==6) { require(pc_seen[5]==address+0x18,FAIL_STEP6_PC); }
    if (stage<7) {
        request("s");
        require(same(wire,wire_size,"+"),FAIL_DETACH);
        stage++;
        publish();
        return 0;
    }
    /* Stage 7: the indirect jump took us back into the caller. */
    /* l.jal sets the link to the instruction after it. */
    require(pc_seen[6]==caller+0x0c,FAIL_RETURN_PC);
    build_request(body,"m",address,0);
    request_word(body,OR1K_TRAP_INSN,FAIL_TRAP_WORD);
    build_request(body,"z0",address,1);
    request_ok(body,FAIL_REMOVE);
    build_request(body,"m",address,0);
    request_word(body,original,FAIL_RESTORED);
    /* Detach answers OK, unlike continue or step, which answer with just the
     * acknowledgement because the target leaves the monitor. */
    request_ok("D",FAIL_DETACH);
    require(!monitor_code_patched(mon),FAIL_PATCH_LEFT);
    /* Detach must not have invented a stop: no patches may be left behind. */
    require(!monitor_code_patched(mon),FAIL_FRAME);
    stage=8;
    publish();
    return 0;
}
