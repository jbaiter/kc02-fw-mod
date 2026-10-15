/* SPDX-License-Identifier: MIT
 * First-stage, checkpoint-only debugger. No exception/breakpoint claims.
 */
#ifndef LUMA_MONITOR_H
#define LUMA_MONITOR_H
#include "debug.h"
#include "usb_debug.h"
#define MONITOR_RAM_LOW 0x02000000u
#define MONITOR_RAM_HIGH 0x027b0000u
#define MONITOR_SCRATCH 0x027b0000u
#define MONITOR_SCRATCH_SIZE 4096u
#define MONITOR_BASE 0x027c0000u

typedef struct {
    void *context;
    int (*read)(void *, u32 address, u8 *, unsigned);
    int (*write)(void *, u32 address, const u8 *, unsigned);
} MonitorMemory;
typedef struct {
    Rsp rsp;
    UsbDebug *usb;
    MonitorMemory memory;
    u32 regs[RSP_REGS];
    u32 return_pc;
    unsigned stopped, resumed, interrupt_requested;
} Monitor;
void monitor_init(Monitor *, UsbDebug *, const MonitorMemory *);
void monitor_enter(Monitor *, const u32 regs[RSP_REGS]);
void monitor_revoke(void *);
/* The board must obtain device-local consent, not a USB control request. */
void monitor_authorize(Monitor *);
void monitor_receive(void *, const u8 *, unsigned);
#endif
