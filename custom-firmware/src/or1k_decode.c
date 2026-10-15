/* SPDX-License-Identifier: MIT */
#include "or1k_decode.h"
int or1k_is_trap(u32 insn)
{
    /* l.trap: opcode 0x08 with bits 25:16 = 0100000000. */
    return (insn >> 26) == 0x08 && ((insn >> 16) & 0x3ff) == 0x100;
}
static u32 sign_extend_26(u32 insn)
{
    /* Bits 25:0 are a signed word offset. Sign-extend, then scale by four. */
    u32 value=insn & 0x03ffffffu;
    if (value & 0x02000000u) { value |= 0xfc000000u; }
    return value << 2;
}
int or1k_successors(u32 pc, u32 insn, const u32 gpr[32], u32 epcr, Or1kSuccessors *out)
{
    u32 op=insn >> 26;
    (void)epcr;
    out->count=0; out->stops=0; out->unknown=0;
    switch (op) {
    case 0x00: /* l.j   */
    case 0x01: /* l.jal */
        out->address[out->count++]=pc+sign_extend_26(insn);
        return 1;
    case 0x04: /* l.bf: taken is the offset, fall-through is pc+4 */
        out->address[out->count++]=pc+sign_extend_26(insn);
        out->address[out->count++]=pc+4;
        return 1;
    case 0x03: /* l.bnf */
        out->address[out->count++]=pc+4;
        out->address[out->count++]=pc+sign_extend_26(insn);
        return 1;
    case 0x11: /* l.jr */
    case 0x12: /* l.jalr */
        out->address[out->count++]=gpr[(insn >> 11) & 0x1f];
        return 1;
    case 0x09: /* l.rfe: PC becomes EPCR and SR is replaced from ESR. */
        out->unknown=1;
        return 0;
    case 0x08:
        if (((insn >> 16) & 0x3ff)==0x100 || ((insn >> 16) & 0x3ff)==0x000) {
            out->stops=1; /* l.trap / l.sys */
            return 1;
        }
        break;
    default:
        break;
    }
    out->address[out->count++]=pc+4;
    return 1;
}
