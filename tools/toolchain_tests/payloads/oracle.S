/* Independent candidate-ABI oracles, not copied compiler output. */
.nodelay
.text
#if CASE == 8
.global assembly_vr
assembly_vr:
 l.mfspr r11,r0,0
 l.jr r9
#elif CASE == 15
.global assembly_calls, asm_direct_return, asm_indirect_return
assembly_calls:
 l.addi r1,r1,-8
 l.sw 4(r1),r9
 l.sw 0(r1),r3
 l.jal record_link
asm_direct_return:
 l.sw 0(r3),r11
 l.movhi r13,hi(record_link)
 l.ori r13,r13,lo(record_link)
 l.jalr r13
asm_indirect_return:
 l.sw 4(r3),r11
 l.addi r11,r0,1
 l.sfeqi r0,0
 l.bf .Ltaken
 l.addi r11,r0,0xBAD
.Ltaken:
 l.sw 8(r3),r11
 l.addi r11,r0,2
 l.sfnei r0,0
 l.bnf .Lbnf_taken
 l.addi r11,r0,0xBAD
.Lbnf_taken:
 l.sw 12(r3),r11
 l.addi r11,r0,0xBAD
 l.sfnei r0,0
 l.bf .Lbf_not_taken
 l.addi r11,r0,3
.Lbf_not_taken:
 l.sw 16(r3),r11
 l.addi r11,r0,0xBAD
 l.sfeqi r0,0
 l.bnf .Lbnf_not_taken
 l.addi r11,r0,4
.Lbnf_not_taken:
 l.sw 20(r3),r11
 l.lwz r9,4(r1)
 l.addi r1,r1,8
 l.jr r9
record_link:
 l.ori r11,r9,0
 l.jr r9
#elif CASE == 12
.global oracle_args, call_c_args
oracle_args:
 l.ori r11,r3,0
#if ARG_COUNT > 1
 l.slli r13,r4,1
 l.add r11,r11,r13
#endif
#if ARG_COUNT > 2
 l.slli r13,r5,2
 l.add r11,r11,r13
#endif
#if ARG_COUNT > 3
 l.slli r13,r6,3
 l.add r11,r11,r13
#endif
#if ARG_COUNT > 4
 l.slli r13,r7,4
 l.add r11,r11,r13
#endif
#if ARG_COUNT > 5
 l.slli r13,r8,5
 l.add r11,r11,r13
#endif
#if ARG_COUNT > 6
 l.lwz r13,0(r1)
 l.slli r13,r13,6
 l.add r11,r11,r13
#endif
#if ARG_COUNT > 7
 l.lwz r13,4(r1)
 l.slli r13,r13,7
 l.add r11,r11,r13
#endif
 l.jr r9
call_c_args:
 l.addi r1,r1,-16
 l.sw 12(r1),r9
 l.ori r13,r3,0
#if ARG_COUNT > 7
 l.lwz r11,28(r13)
 l.sw 4(r1),r11
#endif
#if ARG_COUNT > 6
 l.lwz r11,24(r13)
 l.sw 0(r1),r11
#endif
#if ARG_COUNT > 5
 l.lwz r8,20(r13)
#endif
#if ARG_COUNT > 4
 l.lwz r7,16(r13)
#endif
#if ARG_COUNT > 3
 l.lwz r6,12(r13)
#endif
#if ARG_COUNT > 2
 l.lwz r5,8(r13)
#endif
#if ARG_COUNT > 1
 l.lwz r4,4(r13)
#endif
 l.lwz r3,0(r13)
 l.jal c_args
 l.lwz r9,12(r1)
 l.addi r1,r1,16
 l.jr r9
#elif CASE == 10
.global oracle_wide, call_c_wide
oracle_wide:
 l.xor r11,r3,r6
 l.xor r11,r11,r5
 l.xor r11,r11,r8
 l.lwz r13,0(r1)
 l.xor r11,r11,r13
 l.xor r12,r4,r7
 l.lwz r13,4(r1)
 l.xor r12,r12,r13
 l.movhi r13,0x1234
 l.ori r13,r13,0x5678
 l.xor r12,r12,r13
 l.jr r9
call_c_wide:
 l.addi r1,r1,-16
 l.sw 12(r1),r9
 l.ori r13,r3,0
 l.lwz r11,24(r13)
 l.sw 0(r1),r11
 l.lwz r11,28(r13)
 l.sw 4(r1),r11
 l.lwz r8,20(r13)
 l.lwz r7,16(r13)
 l.lwz r6,12(r13)
 l.lwz r5,8(r13)
 l.lwz r4,4(r13)
 l.lwz r3,0(r13)
 l.jal c_wide
 l.lwz r9,12(r1)
 l.addi r1,r1,16
 l.jr r9
#elif CASE == 11
.global oracle_pair, oracle_big, call_c_pair, call_c_big, memcpy
oracle_pair:
 l.movhi r13,0x1234
 l.ori r13,r13,0x5678
 l.xor r13,r4,r13
 l.sw 0(r3),r13
 l.addi r13,r5,5
 l.sw 4(r3),r13
 l.ori r11,r3,0
 l.jr r9
oracle_big:
 l.ori r11,r3,0
 l.addi r5,r0,8
.Lbig_loop:
 l.sw 0(r3),r4
 l.addi r3,r3,4
 l.addi r4,r4,7
 l.addi r5,r5,-1
 l.sfnei r5,0
 l.bf .Lbig_loop
 l.jr r9
call_c_pair:
 l.addi r1,r1,-8
 l.sw 4(r1),r9
 l.jal c_pair
 l.lwz r9,4(r1)
 l.addi r1,r1,8
 l.jr r9
call_c_big:
 l.addi r1,r1,-8
 l.sw 4(r1),r9
 l.jal c_big
 l.lwz r9,4(r1)
 l.addi r1,r1,8
 l.jr r9
/* A reviewed copy helper for compiler-generated aggregate copies, not libc QA. */
memcpy:
 l.ori r11,r3,0
 l.sfeqi r5,0
 l.bf .Lcopy_done
.Lcopy_loop:
 l.lbz r13,0(r4)
 l.sb 0(r3),r13
 l.addi r3,r3,1
 l.addi r4,r4,1
 l.addi r5,r5,-1
 l.sfnei r5,0
 l.bf .Lcopy_loop
.Lcopy_done:
 l.jr r9
#elif CASE == 7
.global probe_preserved, clobber_caller
probe_preserved:
 l.addi r1,r1,-80
 l.sw 76(r1),r9
 l.sw 16(r1),r2
 l.sw 20(r1),r14
 l.sw 24(r1),r18
 l.sw 28(r1),r20
 l.sw 32(r1),r22
 l.sw 36(r1),r24
 l.sw 40(r1),r26
 l.sw 44(r1),r28
 l.sw 48(r1),r30
 l.sw 52(r1),r10
 l.sw 56(r1),r16
 l.addi r13,r1,80
 l.sw 60(r1),r13
 l.addi r13,r0,0x6B6B
 l.sw 0(r1),r13
 l.sw 0(r4),r1
 l.addi r2,r0,0x1002
 l.addi r14,r0,0x100E
 l.addi r18,r0,0x1012
 l.addi r20,r0,0x1014
 l.addi r22,r0,0x1016
 l.addi r24,r0,0x1018
 l.addi r26,r0,0x101A
 l.addi r28,r0,0x101C
 l.addi r30,r0,0x101E
 l.lwz r3,0(r3)
 l.jal pressure
 /* Recover our SP from the fixed output cell before using any frame slots.
    The absolute address must track the packet layout, so it is built from the
    same defines the C side uses rather than a hardcoded value. */
 l.movhi r13,hi(PACKET_ADDR+OUTPUT_OFFSET)
 l.ori r13,r13,lo(PACKET_ADDR+OUTPUT_OFFSET)
 l.sw 4(r13),r1
 l.lwz r1,0(r13)
 l.sw 8(r13),r11
 l.sw 12(r13),r2
 l.sw 16(r13),r14
 l.sw 20(r13),r18
 l.sw 24(r13),r20
 l.sw 28(r13),r22
 l.sw 32(r13),r24
 l.sw 36(r13),r26
 l.sw 40(r13),r28
 l.sw 44(r13),r30
 l.lwz r11,52(r1)
 l.sw 48(r13),r11
 l.sw 52(r13),r10
 l.lwz r11,56(r1)
 l.sw 56(r13),r11
 l.sw 60(r13),r16
 l.lwz r11,0(r1)
 l.sw 64(r13),r11
 l.lwz r11,60(r1)
 l.sw 68(r13),r11
 l.lwz r2,16(r1)
 l.lwz r14,20(r1)
 l.lwz r18,24(r1)
 l.lwz r20,28(r1)
 l.lwz r22,32(r1)
 l.lwz r24,36(r1)
 l.lwz r26,40(r1)
 l.lwz r28,44(r1)
 l.lwz r30,48(r1)
 l.lwz r10,52(r1)
 l.lwz r16,56(r1)
 l.lwz r9,76(r1)
 l.addi r1,r1,80
 l.jr r9
clobber_caller:
 l.addi r3,r0,0x55
 l.addi r4,r0,0x55
 l.addi r5,r0,0x55
 l.addi r6,r0,0x55
 l.addi r7,r0,0x55
 l.addi r8,r0,0x55
 l.addi r11,r0,0x55
 l.addi r12,r0,0x55
 l.addi r13,r0,0x55
 l.addi r15,r0,0x55
 l.addi r17,r0,0x55
 l.addi r19,r0,0x55
 l.addi r21,r0,0x55
 l.addi r23,r0,0x55
 l.addi r25,r0,0x55
 l.addi r27,r0,0x55
 l.addi r29,r0,0x55
 l.addi r31,r0,0x55
 l.jr r9
#endif
