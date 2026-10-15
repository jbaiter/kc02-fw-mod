/* SPDX-License-Identifier: MIT
 * QEMU-only self-test: a deliberately shaped instruction sequence for the
 * step logic.
 *
 * The conditional branch is taken, so a decoder that only traps pc+4 lands on
 * the not-taken path and the recorded PC sequence stops matching. The final
 * l.jr exercises a successor that comes from a register, and the return
 * address it jumps to sits outside this routine.
 *
 * Registers: r3 = argument and result, r4 = scratch, r9 = link.
 */
.nodelay
.section .probe.text,"ax"

.global bringup_step_caller
bringup_step_caller:
    l.sw    -4(r1),r9          /* keep our own return address */
    l.addi  r3,r0,5            /* argument: non-zero, so l.bnf is taken */
    l.jal   bringup_step_target
    l.nop                      /* +0x0c: the link target */
    l.lwz   r9,-4(r1)
    l.jr    r9
    l.nop

.global bringup_step_target
bringup_step_target:
    l.addi  r4,r0,0            /* +0x00 */
    l.sfeqi r3,0               /* +0x04 */
    l.bnf   .Lstep_taken       /* +0x08: taken for a non-zero argument */
    l.addi  r4,r0,3            /* +0x0c: skipped */
.Lstep_taken:
    l.addi  r3,r3,1            /* +0x10 */
    l.addi  r4,r4,1            /* +0x14 */
    l.jr    r9                 /* +0x18 */
    l.nop                      /* +0x1c */
