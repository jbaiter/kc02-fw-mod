/* SPDX-License-Identifier: MIT
 * Host-test stop monitor. No USB emulation and no live target RAM access. */
#include "debug.h"
static Rsp rsp;
static u8 output[16384], memory[1024];
static unsigned used;
static u32 registers[RSP_REGS];
static int runs, interrupts;
static void send_bytes(void *ctx, const u8 *p, unsigned size)
{
    unsigned i;
    (void)ctx;
    if (size>sizeof(output)-used) { return; }
    for (i=0; i<size; i++) { output[used++]=p[i]; }
}
static int read_reg(void *ctx, unsigned n, u32 *value)
{
    (void)ctx;
    if (n>=RSP_REGS) { return 0; }
    *value=registers[n]; return 1;
}
static int write_reg(void *ctx, unsigned n, u32 value)
{
    (void)ctx;
    if (!n || n>=RSP_REGS) { return 0; }
    registers[n]=value; return 1;
}
static int read_memory(void *ctx, u32 address, u8 *out, unsigned n)
{
    unsigned i;
    (void)ctx;
    if (address>=sizeof(memory) || n>sizeof(memory)-address) { return 0; }
    for (i=0; i<n; i++) { out[i]=memory[address+i]; }
    return 1;
}
static int write_memory(void *ctx, u32 address, const u8 *in, unsigned n)
{
    unsigned i;
    (void)ctx;
    if (address>=sizeof(memory) || n>sizeof(memory)-address) { return 0; }
    for (i=0; i<n; i++) { memory[address+i]=in[i]; }
    return 1;
}
static int resume(void *ctx, int step, int has_address, u32 address)
{
    (void)ctx;
    if (step) { return 0; } /* No fake claim that single-step works. */
    if (has_address) { registers[33]=address; }
    runs++; return 1;
}
static void interrupt(void *ctx) { (void)ctx; interrupts++; rsp_stop(&rsp,2); }
/* This harness has no patches to unwind, so detach is a plain success.
 * Breakpoints stay NULL: framing and registers are what it tests. */
static int detach(void *ctx) { (void)ctx; return 1; }
static const DebugOps ops={0,send_bytes,read_reg,write_reg,read_memory,write_memory,resume,
                           interrupt,0,detach};
void harness_init(int authorised)
{
    unsigned i;
    rsp_init(&rsp,&ops);
    rsp.enabled=authorised;
    used=0; runs=0; interrupts=0;
    for (i=0; i<RSP_REGS; i++) { registers[i]=i*0x01020304u; }
    registers[0]=0; registers[33]=0x02000100;
    for (i=0; i<sizeof(memory); i++) { memory[i]=i; }
}
void harness_feed(const u8 *p, unsigned size)
{
    while (size--) { rsp_feed(&rsp,*p++); }
}
unsigned harness_output(u8 *out)
{
    unsigned i, n=used;
    for (i=0; i<n; i++) { out[i]=output[i]; }
    used=0;
    return n;
}
int harness_runs(void) { return runs; }
int harness_interrupts(void) { return interrupts; }
