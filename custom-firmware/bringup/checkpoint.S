/* SPDX-License-Identifier: MIT
 * Cooperative call boundary only, not an exception/HP-shadow trap handler.
 * Save SR before any arithmetic, then switch to a reserved monitor stack.
 */
.nodelay
.section .text.checkpoint,"ax"
.global bringup_checkpoint
bringup_checkpoint:
 l.sw -132(r1),r3
 l.mfspr r3,r0,0x18
 l.sw -8(r1),r3
 l.addi r1,r1,-144
 l.sw 0(r1),r0
 l.sw 8(r1),r2
 /* r3 was saved before SR access. */
 l.sw 16(r1),r4
 l.sw 20(r1),r5
 l.sw 24(r1),r6
 l.sw 28(r1),r7
 l.sw 32(r1),r8
 l.sw 36(r1),r9
 l.sw 40(r1),r10
 l.sw 44(r1),r11
 l.sw 48(r1),r12
 l.sw 52(r1),r13
 l.sw 56(r1),r14
 l.sw 60(r1),r15
 l.sw 64(r1),r16
 l.sw 68(r1),r17
 l.sw 72(r1),r18
 l.sw 76(r1),r19
 l.sw 80(r1),r20
 l.sw 84(r1),r21
 l.sw 88(r1),r22
 l.sw 92(r1),r23
 l.sw 96(r1),r24
 l.sw 100(r1),r25
 l.sw 104(r1),r26
 l.sw 108(r1),r27
 l.sw 112(r1),r28
 l.sw 116(r1),r29
 l.sw 120(r1),r30
 l.sw 124(r1),r31
 l.addi r3,r1,144
 l.sw 4(r1),r3
 l.addi r3,r9,-4
 l.sw 128(r1),r3
 l.sw 132(r1),r9
 l.or r3,r1,r0
 l.movhi r1,hi(__monitor_stack)
 l.ori r1,r1,lo(__monitor_stack)
 l.jal bringup_service
 /* C returns the application frame pointer in r11 (OpenRISC ABI). */
 l.or r1,r11,r0
 l.lwz r3,136(r1)
 l.mtspr r0,r3,0x18
 l.lwz r2,8(r1)
 l.lwz r3,12(r1)
 l.lwz r4,16(r1)
 l.lwz r5,20(r1)
 l.lwz r6,24(r1)
 l.lwz r7,28(r1)
 l.lwz r8,32(r1)
 l.lwz r9,36(r1)
 l.lwz r10,40(r1)
 l.lwz r11,44(r1)
 l.lwz r12,48(r1)
 l.lwz r13,52(r1)
 l.lwz r14,56(r1)
 l.lwz r15,60(r1)
 l.lwz r16,64(r1)
 l.lwz r17,68(r1)
 l.lwz r18,72(r1)
 l.lwz r19,76(r1)
 l.lwz r20,80(r1)
 l.lwz r21,84(r1)
 l.lwz r22,88(r1)
 l.lwz r23,92(r1)
 l.lwz r24,96(r1)
 l.lwz r25,100(r1)
 l.lwz r26,104(r1)
 l.lwz r27,108(r1)
 l.lwz r28,112(r1)
 l.lwz r29,116(r1)
 l.lwz r30,120(r1)
 l.lwz r31,124(r1)
 l.lwz r1,4(r1)
 l.jr r9
