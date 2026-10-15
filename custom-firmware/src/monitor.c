/* SPDX-License-Identifier: MIT */
#include "monitor.h"
static int range(u32 address, unsigned size, u32 low, u32 high)
{
    return address>=low && address<=high && size<=high-address;
}
static void output(void *ctx, const u8 *p, unsigned n)
{
    Monitor *m=ctx;
    if (!usb_debug_send(m->usb,p,n)) { monitor_revoke(m); }
}
static int read_register(void *ctx, unsigned reg, u32 *value)
{
    Monitor *m=ctx;
    if (!m->stopped || reg>=RSP_REGS) { return 0; }
    *value=m->regs[reg]; return 1;
}
static int write_register(void *ctx, unsigned reg, u32 value)
{
    Monitor *m=ctx;
    /* SP, link, PC and SR stay immutable in the checkpoint-only milestone. */
    if (!m->stopped || reg<2 || reg==9 || reg>=32) { return 0; }
    m->regs[reg]=value; return 1;
}
static int read_memory(void *ctx, u32 address, u8 *out, unsigned n)
{
    Monitor *m=ctx;
    if (!m->stopped || !m->memory.read || n>256 ||
        !range(address,n,MONITOR_RAM_LOW,MONITOR_SCRATCH+MONITOR_SCRATCH_SIZE)) { return 0; }
    return m->memory.read(m->memory.context,address,out,n);
}
static int write_memory(void *ctx, u32 address, const u8 *data, unsigned n)
{
    Monitor *m=ctx;
    /* Scratch only: no executable writes or cache-coherence assumptions. */
    if (!m->stopped || !m->memory.write || n>256 ||
        !range(address,n,MONITOR_SCRATCH,MONITOR_SCRATCH+MONITOR_SCRATCH_SIZE)) { return 0; }
    return m->memory.write(m->memory.context,address,data,n);
}
static int resume(void *ctx, int step, int has_address, u32 address)
{
    Monitor *m=ctx;
    if (!m->stopped || step || (has_address && address!=m->return_pc)) { return 0; }
    m->resumed=1; return 1;
}
static void interrupt(void *ctx) { ((Monitor *)ctx)->interrupt_requested=1; }
void monitor_revoke(void *ctx)
{
    Monitor *m=ctx;
    DebugOps ops=m->rsp.ops;
    rsp_init(&m->rsp,&ops);
    m->resumed=0; m->interrupt_requested=0;
    /* Remain stopped. Unplug/reset cannot silently run an inspected target. */
}
void monitor_authorize(Monitor *m)
{
    if (m->stopped && m->usb->configured && !m->usb->fault) { m->rsp.enabled=1; }
}
void monitor_init(Monitor *m, UsbDebug *usb, const MonitorMemory *memory)
{
    DebugOps ops={m,output,read_register,write_register,read_memory,write_memory,resume,interrupt};
    *m=(Monitor){.usb=usb,.memory=*memory}; rsp_init(&m->rsp,&ops);
}
void monitor_enter(Monitor *m, const u32 regs[RSP_REGS])
{
    unsigned i;
    for (i=0; i<RSP_REGS; i++) { m->regs[i]=regs[i]; }
    m->regs[0]=0; m->return_pc=regs[33]; m->stopped=1; m->resumed=0;
    m->interrupt_requested=0;
    rsp_stop(&m->rsp,5);
}
void monitor_receive(void *ctx, const u8 *p, unsigned n)
{
    Monitor *m=ctx;
    unsigned i;
    for (i=0; i<n; i++) {
        if (m->usb->fault) { monitor_revoke(m); return; }
        rsp_feed(&m->rsp,p[i]);
    }
}
