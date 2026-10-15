/* SPDX-License-Identifier: MIT
 * Minimal OpenRISC successor decoder for the step-over logic.
 *
 * Only control flow matters here: everything else falls through to pc+4. The
 * opcode table is taken from QEMU's own target/or1k/insns.decode, and the
 * functional behaviour is validated by stepping in QEMU and comparing against
 * where execution actually landed. A wrong decode shows up as a step that
 * lands somewhere else, not as a silent misread.
 *
 * No-delay only. A delay-slot core needs the branch and its slot handled as
 * one unit, which this build never encounters.
 */
#ifndef LUMA_OR1K_DECODE_H
#define LUMA_OR1K_DECODE_H
#include "luma.h"
#define OR1K_SUCCESSORS 2
/* l.trap and l.sys are the only encodings that stop at the target. */
#define OR1K_TRAP_INSN 0x21000000u
typedef struct {
    unsigned count;
    u32 address[OR1K_SUCCESSORS];
    int stops;      /* Execution does not continue past this instruction. */
    int unknown;    /* Successor depends on state this decoder will not guess. */
} Or1kSuccessors;
/* `epcr` is only consulted for l.rfe, which is reported as unknown so callers
 * can refuse to step over it instead of picking a successor. */
int or1k_successors(u32 pc, u32 insn, const u32 gpr[32], u32 epcr, Or1kSuccessors *out);
int or1k_is_trap(u32 insn);
#endif
