/* SPDX-License-Identifier: MIT
 * Original KC02 factory application handoff. No NOR operations.
 * Factory boot has already configured clocks/SDRAM; this is not a ROM loader.
 */
.nodelay
.section .boot,"ax"
.space 0x46c
.global _start
_start:
 l.mtspr r0,r0,0x4800
 l.mfspr r3,r0,0x18
 l.movhi r4,0xffff
 l.ori r4,r4,0xfff9
 l.and r3,r3,r4
 l.mtspr r0,r3,0x18
 /* Refuse a warm handoff with dirty/active D-cache. No discard of live data. */
 l.mfspr r3,r0,0x1800
 l.andi r3,r3,1
 l.sfnei r3,0
 l.bf .Lunsafe
 l.mtspr r0,r0,0x2000
 l.mtspr r0,r0,0x2008
 l.movhi r3,hi(__monitor_load)
 l.ori r3,r3,lo(__monitor_load)
 l.movhi r4,hi(__monitor_start)
 l.ori r4,r4,lo(__monitor_start)
 l.movhi r5,hi(__monitor_end)
 l.ori r5,r5,lo(__monitor_end)
.Lcopy:
 l.sfgeu r4,r5
 l.bf .Lzero_setup
 l.lwz r6,0(r3)
 l.sw 0(r4),r6
 l.addi r3,r3,4
 l.addi r4,r4,4
 l.j .Lcopy
.Lzero_setup:
 l.movhi r3,hi(__bss_start)
 l.ori r3,r3,lo(__bss_start)
 l.movhi r4,hi(__bss_end)
 l.ori r4,r4,lo(__bss_end)
.Lzero:
 l.sfgeu r3,r4
 l.bf .Lrun
 l.sw 0(r3),r0
 l.addi r3,r3,4
 l.j .Lzero
.Lrun:
 l.movhi r1,hi(__monitor_stack)
 l.ori r1,r1,lo(__monitor_stack)
 l.movhi r2,hi(bringup_main)
 l.ori r2,r2,lo(bringup_main)
 l.jr r2
.Lunsafe:
 l.j .Lunsafe
