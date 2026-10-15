/* SPDX-License-Identifier: MIT
 * Bounded GDB Remote Serial Protocol engine, transport independent.
 * Not a USB controller/exception backend. All callbacks must be provided by
 * the privileged stop monitor, never by the application loop being debugged.
 */
#ifndef LUMA_DEBUG_H
#define LUMA_DEBUG_H
#include "luma.h"
#define RSP_CAP 1024
#define RSP_REGS 35 /* r0..r31, ppc, npc, sr; 32-bit LITTLE-ENDIAN each */
typedef struct {
    void *context;
    void (*send)(void *context, const u8 *data, unsigned size);
    int (*reg_read)(void *context, unsigned reg, u32 *value);
    int (*reg_write)(void *context, unsigned reg, u32 value);
    int (*mem_read)(void *context, u32 address, u8 *out, unsigned size);
    int (*mem_write)(void *context, u32 address, const u8 *data, unsigned size);
    int (*resume)(void *context, int step, int has_address, u32 address);
    void (*interrupt)(void *context);
    /* Insert or remove a software breakpoint. Absent means the target has no
     * breakpoint support, and Z0/z0 answer as unsupported. */
    int (*breakpoint)(void *context, u32 address, int insert);
    /* Let the target run with every debugger change undone. Separate from
     * resume, because detaching from a breakpoint must lift that breakpoint
     * rather than step over it: there will be nobody left to service the
     * temporary trap a step-over would install. */
    int (*detach)(void *context);
} DebugOps;
typedef struct {
    DebugOps ops;
    int enabled; /* Only after explicit device-local debug authorisation. */
    unsigned state, length, checksum, high, escaped, overflow;
    unsigned last_size;
    u8 packet[RSP_CAP], last[RSP_CAP+4];
} Rsp;
void rsp_init(Rsp *rsp, const DebugOps *ops);
void rsp_feed(Rsp *rsp, u8 byte);
void rsp_stop(Rsp *rsp, unsigned signal);
#endif
