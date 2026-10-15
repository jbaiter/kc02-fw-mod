/* SPDX-License-Identifier: MIT
 * Exception vectors for the debug monitor.
 *
 * The vendor's reset.S defines the vector layout as 16-byte slots at
 * .org 0x0000, 0x0010, ... holding one l.j each, ordered buserr, dpf, ipf,
 * lpint, align, instruction, hpint, ... That order is exactly QEMU's EXCP_*
 * numbering, so the slot index is (exception << 4) and EVBA selects the table.
 *
 * Hardware caveat: the SDK's SR bit 14 is named intvct_sram/intvct_mask and is
 * documented as selecting SRAM versus mask-ROM vectors. This image leaves it
 * clear and points EVBA at its own table in SDRAM. The emulator dispatches to
 * EVBA regardless. Whether silicon requires the SRAM-vector variant instead is
 * unverified and has to be confirmed on a real unit.
 *
 * Frames are 160 bytes, indexed by register number: word i lives at offset 4*i
 * for i = 0..34, and word 35 holds the exception number. Both the checkpoint
 * path and this one hand C the same shape.
 */
.nodelay
.section .vectors,"ax"

.macro SLOT num, stub
    .org    \num * 16
    l.j     \stub
    l.nop
    l.nop
    l.nop
.endm

    SLOT    0,  bringup_exc_stub0
    SLOT    1,  bringup_exc_stub1
    SLOT    2,  bringup_exc_stub2
    SLOT    3,  bringup_exc_stub3
    SLOT    4,  bringup_exc_stub4
    SLOT    5,  bringup_exc_stub5
    SLOT    6,  bringup_exc_stub6
    SLOT    7,  bringup_exc_stub7
    SLOT    8,  bringup_exc_stub8
    SLOT    9,  bringup_exc_stub9
    SLOT    10, bringup_exc_stub10
    SLOT    11, bringup_exc_stub11
    SLOT    12, bringup_exc_stub12
    SLOT    13, bringup_exc_stub13
    SLOT    14, bringup_exc_stub14
    SLOT    15, bringup_exc_stub15

.section .text,"ax"

/* Each stub parks the interrupted stack pointer by 160 bytes, records the
 * original r3, then replaces r3 with the exception number. Saving r3 first is
 * what keeps the number from destroying live target state. */
.macro STUB name, num
    .global \name
\name:
    l.addi  r1,r1,-160
    l.sw    12(r1),r3
    l.addi  r3,r0,\num
    l.j     bringup_exception_common
.endm

    STUB    bringup_exc_stub0,  0
    STUB    bringup_exc_stub1,  1
    STUB    bringup_exc_stub2,  2
    STUB    bringup_exc_stub3,  3
    STUB    bringup_exc_stub4,  4
    STUB    bringup_exc_stub5,  5
    STUB    bringup_exc_stub6,  6
    STUB    bringup_exc_stub7,  7
    STUB    bringup_exc_stub8,  8
    STUB    bringup_exc_stub9,  9
    STUB    bringup_exc_stub10, 10
    STUB    bringup_exc_stub11, 11
    STUB    bringup_exc_stub12, 12
    STUB    bringup_exc_stub13, 13
    STUB    bringup_exc_stub14, 14
    STUB    bringup_exc_stub15, 15

.global bringup_exception_common
bringup_exception_common:
    l.sw    0(r1),r0
    l.sw    8(r1),r2
    l.sw    16(r1),r4
    l.sw    20(r1),r5
    l.sw    24(r1),r6
    l.sw    28(r1),r7
    l.sw    32(r1),r8
    l.sw    36(r1),r9
    l.sw    40(r1),r10
    l.sw    44(r1),r11
    l.sw    48(r1),r12
    l.sw    52(r1),r13
    l.sw    56(r1),r14
    l.sw    60(r1),r15
    l.sw    64(r1),r16
    l.sw    68(r1),r17
    l.sw    72(r1),r18
    l.sw    76(r1),r19
    l.sw    80(r1),r20
    l.sw    84(r1),r21
    l.sw    88(r1),r22
    l.sw    92(r1),r23
    l.sw    96(r1),r24
    l.sw    100(r1),r25
    l.sw    104(r1),r26
    l.sw    108(r1),r27
    l.sw    112(r1),r28
    l.sw    116(r1),r29
    l.sw    120(r1),r30
    l.sw    124(r1),r31
    /* The interrupted stack pointer, before the frame was pushed. */
    l.addi  r2,r1,160
    l.sw    4(r1),r2
    /* ESR holds the pre-exception SR, which rfe must restore. */
    l.mfspr r2,r0,0x40
    l.sw    136(r1),r2
    /* EPCR is the trapping instruction, which is the resume point, so it goes
     * in the npc slot. ppc is derived rather than recorded because the
     * exception entry does not preserve the previous instruction's address. */
    l.mfspr r2,r0,0x20
    l.sw    132(r1),r2
    l.addi  r2,r2,-4
    l.sw    128(r1),r2
    l.sw    140(r1),r3
    /* Move to the reserved monitor stack and hand C the frame. */
    l.or    r3,r1,r0
    l.movhi r1,hi(__monitor_stack)
    l.ori   r1,r1,lo(__monitor_stack)
    l.jal   bringup_exception_service
    l.or    r1,r11,r0
    l.lwz   r2,136(r1)
    l.mtspr r0,r2,0x40
    l.lwz   r2,132(r1)
    l.mtspr r0,r2,0x20
    l.lwz   r2,8(r1)
    l.lwz   r3,12(r1)
    l.lwz   r4,16(r1)
    l.lwz   r5,20(r1)
    l.lwz   r6,24(r1)
    l.lwz   r7,28(r1)
    l.lwz   r8,32(r1)
    l.lwz   r9,36(r1)
    l.lwz   r10,40(r1)
    l.lwz   r11,44(r1)
    l.lwz   r12,48(r1)
    l.lwz   r13,52(r1)
    l.lwz   r14,56(r1)
    l.lwz   r15,60(r1)
    l.lwz   r16,64(r1)
    l.lwz   r17,68(r1)
    l.lwz   r18,72(r1)
    l.lwz   r19,76(r1)
    l.lwz   r20,80(r1)
    l.lwz   r21,84(r1)
    l.lwz   r22,88(r1)
    l.lwz   r23,92(r1)
    l.lwz   r24,96(r1)
    l.lwz   r25,100(r1)
    l.lwz   r26,104(r1)
    l.lwz   r27,108(r1)
    l.lwz   r28,112(r1)
    l.lwz   r29,116(r1)
    l.lwz   r30,120(r1)
    l.lwz   r31,124(r1)
    l.lwz   r1,4(r1)
    l.rfe
