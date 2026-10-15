/* SPDX-License-Identifier: MIT */
#include "monitor.h"
#include "or1k_decode.h"
static int inside(u32 address, unsigned size, u32 low, u32 high)
{
    return address>=low && address<=high && size<=high-address;
}
static int word_readable(Monitor *m, u32 address, u32 *value)
{
    return address%4==0 && m->memory.read &&
           inside(address,4,m->regions.ram_low,m->regions.ram_high) &&
           m->memory.read(m->memory.context,address,(u8 *)value,4);
}
static void code_sync(Monitor *m, u32 address)
{
    if (m->memory.code_sync) { m->memory.code_sync(m->memory.context,address,4); }
}
/* Writes a trap word over one instruction, or puts the original back. Every
 * patch is recorded so detach can undo it. */
static int patch_write(Monitor *m, MonitorPatch *p, u32 address, u32 value)
{
    u32 word=value;
    if (!inside(address,4,m->regions.code_low,m->regions.code_high)) { return 0; }
    if (!m->memory.write || !m->memory.write(m->memory.context,address,(const u8 *)&word,4)) {
        return 0;
    }
    code_sync(m,address);
    p->address=address;
    return 1;
}
static int patch_arm(Monitor *m, MonitorPatch *p, u32 address)
{
    u32 original;
    if (p->live) { return 1; }
    /* A disarmed slot holds nothing, so it can be re-aimed at a new address.
     * This is what lets the two temporary slots follow successive steps. */
    if (p->used && p->address!=address) { p->used=0; }
    if (!p->used) {
        if (!word_readable(m,address,&original) || or1k_is_trap(original)) { return 0; }
        p->original=original;
        p->address=address;
        p->used=1;
    }
    if (!patch_write(m,p,address,OR1K_TRAP_INSN)) { p->used=0; return 0; }
    p->live=1;
    return 1;
}
static int patch_disarm(Monitor *m, MonitorPatch *p)
{
    int ok=1;
    if (p->live) { ok=patch_write(m,p,p->address,p->original); }
    p->live=0;
    return ok;
}
/* Releases the slot as well as the trap word. */
static int patch_drop(Monitor *m, MonitorPatch *p)
{
    int ok=patch_disarm(m,p);
    p->used=0;
    return ok;
}
static void unwind(Monitor *m)
{
    unsigned i;
    for (i=0; i<MON_TEMPORARY; i++) { patch_drop(m,&m->temporary[i]); }
    for (i=0; i<MON_BREAKPOINTS; i++) { patch_drop(m,&m->breakpoints[i]); }
}
/* An armed trap at this address. */
static MonitorPatch *find_live(Monitor *m, MonitorPatch *set, unsigned count, u32 address)
{
    unsigned i;
    (void)m;
    for (i=0; i<count; i++) {
        if (set[i].live && set[i].address==address) { return &set[i]; }
    }
    return 0;
}
/* A slot that owns this address, armed or temporarily disarmed. */
static MonitorPatch *find_slot(Monitor *m, MonitorPatch *set, unsigned count, u32 address)
{
    unsigned i;
    (void)m;
    for (i=0; i<count; i++) {
        if (set[i].used && set[i].address==address) { return &set[i]; }
    }
    return 0;
}
static MonitorPatch *claim(Monitor *m, MonitorPatch *set, unsigned count)
{
    unsigned i;
    (void)m;
    for (i=0; i<count; i++) { if (!set[i].live) { return &set[i]; } }
    return 0;
}
static void removed_add(Monitor *m, u32 address)
{
    unsigned i;
    for (i=0; i<MON_BREAKPOINTS; i++) {
        if (m->removed[i]==address) { return; }
        if (!m->removed[i]) { m->removed[i]=address; return; }
    }
}
static void removed_clear(Monitor *m)
{
    unsigned i;
    for (i=0; i<MON_BREAKPOINTS; i++) { m->removed[i]=0; }
}
/* Puts back breakpoints that were lifted to step over them. */
static int removed_rearm(Monitor *m, u32 except)
{
    unsigned i;
    int ok=1;
    for (i=0; i<MON_BREAKPOINTS; i++) {
        u32 address=m->removed[i];
        MonitorPatch *p;
        if (!address) { continue; }
        m->removed[i]=0;
        if (address==except) { continue; }
        p=find_slot(m,m->breakpoints,MON_BREAKPOINTS,address);
        if (p && !patch_arm(m,p,address)) { ok=0; }
    }
    return ok;
}
static int breakpoint_set(Monitor *m, u32 address, int insert)
{
    MonitorPatch *p=find_slot(m,m->breakpoints,MON_BREAKPOINTS,address);
    if (!insert) {
        if (!p) { return 0; }
        return patch_drop(m,p);
    }
    if (p) { return 1; }
    p=claim(m,m->breakpoints,MON_BREAKPOINTS);
    return p && patch_arm(m,p,address);
}
static void output(void *ctx, const u8 *p, unsigned n)
{
    Monitor *m=ctx;
    if (!m->usb || !usb_device_send(m->usb,p,n)) { m->link_failed=1; }
}
static int read_register(void *ctx, unsigned reg, u32 *value)
{
    Monitor *m=ctx;
    if (reg>=RSP_REGS) { return 0; }
    *value=m->regs[reg]; return 1;
}
static int write_register(void *ctx, unsigned reg, u32 value)
{
    Monitor *m=ctx;
    /* SP, link register and SR stay fixed: a bad value there stops the monitor
     * rather than the target. `npc` is writable for a trap stop only, and only
     * into a region a trap may be placed at. */
    if (reg==33) {
        if (m->stop_reason==MON_STOP_CHECKPOINT ||
            !inside(value,4,m->regions.code_low,m->regions.code_high)) { return 0; }
        m->regs[reg]=value; return 1;
    }
    if (reg<2 || reg==9 || reg>=32) { return 0; }
    m->regs[reg]=value; return 1;
}
static int read_memory(void *ctx, u32 address, u8 *out, unsigned n)
{
    Monitor *m=ctx;
    if (n>256 || !inside(address,n,m->regions.ram_low,m->regions.ram_high)) { return 0; }
    return m->memory.read(m->memory.context,address,out,n);
}
static int write_memory(void *ctx, u32 address, const u8 *data, unsigned n)
{
    Monitor *m=ctx;
    if (n>256 || !inside(address,n,m->regions.scratch_low,m->regions.scratch_high)) { return 0; }
    return m->memory.write(m->memory.context,address,data,n);
}
/* Places temporary traps on the successors of the instruction at `pc`, having
 * first put back that instruction's own original word. */
static int step_arm(Monitor *m, u32 pc, u32 insn)
{
    Or1kSuccessors next;
    MonitorPatch *bp=find_live(m,m->breakpoints,MON_BREAKPOINTS,pc);
    unsigned i;
    if (m->temporary[0].live || m->temporary[1].live) { return 0; }
    if (!or1k_successors(pc,insn,m->regs,0,&next) || next.unknown || next.stops) { return 0; }
    if (!next.count || next.count>MON_TEMPORARY) { return 0; }
    /* A branch back to its own address would re-trap at the instruction the
     * step is trying to leave. Refuse rather than hide the stop. */
    for (i=0; i<next.count; i++) {
        if (next.address[i]==pc) { return 0; }
    }
    if (bp) {
        if (!patch_disarm(m,bp)) { return 0; }
        removed_add(m,pc);
    }
    for (i=0; i<next.count; i++) {
        /* A real breakpoint there already supplies the trap. */
        if (find_live(m,m->breakpoints,MON_BREAKPOINTS,next.address[i])) { continue; }
        if (!patch_arm(m,&m->temporary[i],next.address[i])) {
            unsigned j;
            for (j=0; j<i; j++) { patch_disarm(m,&m->temporary[j]); }
            removed_rearm(m,pc);
            return 0;
        }
    }
    return 1;
}
static int resume(void *ctx, int step, int has_address, u32 address)
{
    Monitor *m=ctx;
    u32 pc=m->regs[33], insn;
    MonitorPatch *bp;
    if (!m->rsp.enabled) { return 0; }
    if (has_address) { pc=address; m->regs[33]=pc; }
    if (pc%4) { return 0; }
    m->suppress=0;
    bp=find_live(m,m->breakpoints,MON_BREAKPOINTS,pc);
    if (step) {
        insn=bp ? bp->original : 0;
        if (!bp && !word_readable(m,pc,&insn)) { return 0; }
        if (!step_arm(m,pc,insn)) { return 0; }
    } else if (bp) {
        /* Continuing from a breakpoint means stepping over it, then hiding the
         * intermediate stop so the debugger only sees the next real event. */
        if (!step_arm(m,pc,bp->original)) { return 0; }
        m->suppress=1;
    } else if (!removed_rearm(m,pc)) {
        return 0;
    }
    m->leave=1;
    return 1;
}
static int breakpoint(void *ctx, u32 address, int insert)
{
    Monitor *m=ctx;
    if (address%4) { return 0; }
    return breakpoint_set(m,address,insert);
}
/* Lifts every patch, disables the session and releases the target. The target
 * re-executes whatever instruction it was stopped at, whose original bytes are
 * back in place, so nothing is skipped and nothing runs twice. */
static int detach(void *ctx)
{
    monitor_revoke(ctx);
    return 1;
}
static void interrupt(void *ctx)
{
    Monitor *m=ctx;
    /* A free-running target cannot be halted from here yet: this takes effect
     * at the next checkpoint or trap. Reporting a stop that has not happened
     * would be a lie, so the request is recorded and nothing else. */
    m->interrupt_requested=1;
}
/* Decides what stopped execution and cleans up the traps that caused it. */
static void classify(Monitor *m)
{
    u32 pc=m->regs[33];
    MonitorPatch *bp=find_live(m,m->breakpoints,MON_BREAKPOINTS,pc);
    MonitorPatch *temp=find_live(m,m->temporary,MON_TEMPORARY,pc);
    unsigned i;
    for (i=0; i<MON_TEMPORARY; i++) {
        if (&m->temporary[i]==temp) {
            /* A real breakpoint at the same address owns the trap word. */
            if (bp) { m->temporary[i].live=0; m->temporary[i].used=0; }
            else { patch_disarm(m,&m->temporary[i]); }
        } else {
            patch_disarm(m,&m->temporary[i]);
        }
    }
    if (bp) { m->stop_reason=MON_STOP_BREAKPOINT; }
    else if (temp) { m->stop_reason=MON_STOP_STEP; }
    else { m->stop_reason=MON_STOP_EXCEPTION; }
    if (!removed_rearm(m,pc)) { m->stop_reason=MON_STOP_EXCEPTION; }
}
static void report(Monitor *m)
{
    unsigned signal=m->stop_reason==MON_STOP_EXCEPTION ? 7 : 5;
    rsp_stop(&m->rsp,signal);
}
/* Runs until the debugger lets the target go. */
static void wait_stopped(Monitor *m)
{
    m->leave=0;
    while (!m->leave) {
        if (m->probe && m->probe(m->probe_context,m)) { break; }
        if (m->poll) { m->poll(m->poll_context); }
        if (m->link_failed) { monitor_revoke(m); break; }
    }
}
static void take_frame(Monitor *m, u32 *frame)
{
    unsigned i;
    for (i=0; i<RSP_REGS; i++) { m->regs[i]=frame[i]; }
    m->regs[0]=0;
    m->exception=frame[RSP_REGS];
}
static void give_frame(Monitor *m, u32 *frame)
{
    unsigned i;
    for (i=0; i<RSP_REGS; i++) { frame[i]=m->regs[i]; }
}
u32 *monitor_checkpoint(Monitor *m, u32 *frame)
{
    take_frame(m,frame);
    m->stop_reason=MON_STOP_CHECKPOINT;
    m->suppress=0;
    report(m);
    wait_stopped(m);
    give_frame(m,frame);
    return frame;
}
u32 *monitor_exception(Monitor *m, u32 *frame)
{
    take_frame(m,frame);
    if (m->handling) {
        /* A fault inside the monitor would re-enter it and overwrite the frame
         * it is still using. Stop dead instead of recursing. */
        for (;;) { }
    }
    m->handling=1;
    classify(m);
    if (!m->suppress) { report(m); }
    else { m->suppress=0; }
    wait_stopped(m);
    m->handling=0;
    give_frame(m,frame);
    return frame;
}
void monitor_revoke(void *ctx)
{
    Monitor *m=ctx;
    DebugOps ops=m->rsp.ops;
    if (m->revoking) { m->leave=1; return; }
    m->revoking=1;
    unwind(m);
    removed_clear(m);
    rsp_init(&m->rsp,&ops);
    m->leave=1; m->suppress=0; m->interrupt_requested=0; m->link_failed=0;
    m->revoking=0;
    /* Remain stopped. Unplug or reset never silently resumes the target. */
}
void monitor_authorize(Monitor *m)
{
    if (!m->handling && m->usb && m->usb->configured && !m->usb->fault) { m->rsp.enabled=1; }
}
void monitor_init(Monitor *m, UsbDevice *usb, const MonitorMemory *memory, const MonitorRegions *regions)
{
    DebugOps ops={m,output,read_register,write_register,read_memory,write_memory,resume,
                  interrupt,breakpoint,detach};
    *m=(Monitor){.usb=usb,.memory=*memory,.regions=*regions};
    rsp_init(&m->rsp,&ops);
}
void monitor_receive(void *ctx, const u8 *p, unsigned n)
{
    Monitor *m=ctx;
    unsigned i;
    for (i=0; i<n; i++) {
        if (m->usb->fault) { monitor_revoke(m); return; }
        rsp_feed(&m->rsp,p[i]);
    }
    if (m->link_failed) { monitor_revoke(m); }
}
int monitor_code_patched(const Monitor *m)
{
    unsigned i;
    for (i=0; i<MON_BREAKPOINTS; i++) { if (m->breakpoints[i].live) { return 1; } }
    for (i=0; i<MON_TEMPORARY; i++) { if (m->temporary[i].live) { return 1; } }
    return 0;
}
