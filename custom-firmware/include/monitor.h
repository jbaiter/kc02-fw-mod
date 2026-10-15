/* SPDX-License-Identifier: MIT
 * Target-side debug monitor: breakpoints, single-step and checked access.
 *
 * Two entry paths reach it. `monitor_checkpoint` is the cooperative call
 * boundary used by the workload. `monitor_exception` is the real exception
 * entry, driven by a vector table the target installs, so a trap can stop
 * execution at an arbitrary instruction.
 *
 * Everything the monitor patches into target memory is remembered and
 * restored on detach, revoke and bus reset. Nothing is left modified.
 */
#ifndef LUMA_MONITOR_H
#define LUMA_MONITOR_H
#include "debug.h"
#include "usb_device.h"
#define MON_BREAKPOINTS 8
#define MON_TEMPORARY 2
#define MON_RAM_LOW 0x02000000u
#define MON_RAM_HIGH 0x027b0000u
#define MON_SCRATCH 0x027b0000u
#define MON_SCRATCH_SIZE 4096u

typedef struct {
    void *context;
    int (*read)(void *, u32 address, u8 *out, unsigned size);
    int (*write)(void *, u32 address, const u8 *data, unsigned size);
    /* Instruction-cache maintenance after patching code. Optional: a target
     * with coherent memory passes NULL. A real one must run the vendor
     * invalidate sequence, because the trap word has to be visible to fetch. */
    void (*code_sync)(void *, u32 address, unsigned size);
} MonitorMemory;

typedef struct {
    u32 code_low, code_high;         /* Addresses a trap may be placed at. */
    u32 ram_low, ram_high;           /* Addresses the debugger may read. */
    u32 scratch_low, scratch_high;   /* The only addresses it may write. */
} MonitorRegions;

/* `used` keeps a slot's identity while it is temporarily disarmed, so a
 * step-over can put the same breakpoint back. `live` means the trap word is
 * currently in memory. */
typedef struct { u32 address, original; int live, used; } MonitorPatch;

enum {
    MON_STOP_CHECKPOINT, MON_STOP_BREAKPOINT, MON_STOP_STEP, MON_STOP_EXCEPTION
};

struct Monitor;
typedef int (*MonitorProbe)(void *context, struct Monitor *monitor);

typedef struct Monitor {
    Rsp rsp;
    UsbDevice *usb;
    MonitorMemory memory;
    MonitorRegions regions;
    u32 regs[RSP_REGS];
    u32 exception;
    unsigned stop_reason;
    MonitorPatch breakpoints[MON_BREAKPOINTS];
    MonitorPatch temporary[MON_TEMPORARY];
    u32 removed[MON_BREAKPOINTS];   /* Breakpoints lifted for a step-over. */
    int handling;                   /* Non-zero while the monitor itself runs. */
    int revoking;
    int leave, suppress, stopping, link_failed;
    int interrupt_requested;
    /* The transport is serviced from here while the target is stopped. */
    void (*poll)(void *context);
    void *poll_context;
    /* Optional test hook, called while stopped before the transport poll.
     * Returning non-zero releases the target without a debugger request. */
    MonitorProbe probe;
    void *probe_context;
} Monitor;

void monitor_init(Monitor *, UsbDevice *, const MonitorMemory *, const MonitorRegions *);
void monitor_authorize(Monitor *);
void monitor_revoke(void *);
void monitor_receive(void *, const u8 *, unsigned);
u32 *monitor_checkpoint(Monitor *, u32 *frame);
u32 *monitor_exception(Monitor *, u32 *frame);
int monitor_code_patched(const Monitor *);
#endif
