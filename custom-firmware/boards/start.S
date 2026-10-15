/* SPDX-License-Identifier: MIT */
.nodelay
.section .text.start,"ax"
.global _start
_start:
 l.movhi r1,hi(__stack_top)
 l.ori r1,r1,lo(__stack_top)
 l.addi r2,r0,0
 /* This development image polls peripherals; no vendor vectors or RTOS. */
 l.mtspr r0,r0,0x4800
 l.movhi r3,hi(__bss_start)
 l.ori r3,r3,lo(__bss_start)
 l.movhi r4,hi(__bss_end)
 l.ori r4,r4,lo(__bss_end)
.Lzero:
 l.sfgeu r3,r4
 l.bf .Lmain
 l.sw 0(r3),r0
 l.addi r3,r3,4
 l.j .Lzero
.Lmain:
 l.jal firmware_main
.Lhalt:
 l.j .Lhalt
