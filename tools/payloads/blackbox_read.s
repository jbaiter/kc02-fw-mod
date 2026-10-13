/*
 * blackbox_read.s -- firmware-independent CPU-memory read for AX3292 KC02s.
 *
 * Staged into the fixed USB OUT endpoint buffer at 0x8250 (entry 0x8290) and
 * invoked through vendor command 0xCD with Func1 = 0x8290:
 *
 *     r3 = &cmd_ctx   r4 = &xport_ctx   r9 = return address
 *
 * It copies MscCmd.DataAddr[0 .. MscCmd.Residue) through the fixed TX endpoint
 * buffer at 0x8040 and streams it back with the firmware's own bulk-TX function
 * pointer (xport_ctx+0x24).  This avoids the USB DMA's short address window,
 * which is why direct reads of SDRAM alias into low SRAM.
 *
 * Layout used (identical on both known builds):
 *   cmd_ctx + 0x24  Residue  (= CBW transfer length)
 *   cmd_ctx + 0x38  DataAddr (source address)
 *   xport_ctx + 0x01 endpoint number
 *   xport_ctx + 0x24 fp_usb_bulk_tx
 *
 * Assembled with:  or1k-elf-as -o blackbox_read.o blackbox_read.s
 *                  or1k-elf-objcopy -O binary blackbox_read.o blackbox_read.bin
 */
	.text
	.global _start
_start:
	l.sw	-4(r1), r9
	l.sw	-8(r1), r2
	l.sw	-12(r1), r14
	l.sw	-16(r1), r15
	l.sw	-20(r1), r16
	l.sw	-24(r1), r18
	l.sw	-28(r1), r19
	l.addi	r1, r1, -28
	l.ori	r14, r3, 0         /* &cmd_ctx */
	l.ori	r2, r4, 0          /* &xport_ctx */
	l.lwz	r19, 56(r14)       /* src = DataAddr */
	l.lwz	r15, 36(r14)       /* len = Residue */
	l.lwz	r18, 36(r2)        /* fp_usb_bulk_tx */
	l.lbz	r16, 1(r2)         /* ep */
loop:
	l.sfeqi	r15, 0
	l.bf	done
	l.ori	r8, r0, 512
	l.sfgtu	r15, r8
	l.bf	have
	l.ori	r8, r15, 0         /* chunk = min(len, 512) */
have:
	l.movhi	r7, 0x0
	l.ori	r7, r7, 0x8040     /* dest = TX endpoint buffer */
	l.ori	r9, r0, 0
copy:
	l.sfeq	r9, r8
	l.bf	send
	l.add	r10, r19, r9
	l.lbz	r12, 0(r10)
	l.add	r10, r7, r9
	l.sb	0(r10), r12
	l.addi	r9, r9, 1
	l.j	copy
send:
	l.ori	r3, r16, 0
	l.ori	r4, r7, 0
	l.ori	r5, r8, 0
	l.add	r19, r19, r8
	l.sub	r15, r15, r8
	l.jalr	r18
	l.j	loop
done:
	l.addi	r1, r1, 28
	l.lwz	r9, -4(r1)
	l.lwz	r2, -8(r1)
	l.lwz	r14, -12(r1)
	l.lwz	r15, -16(r1)
	l.lwz	r16, -20(r1)
	l.lwz	r18, -24(r1)
	l.lwz	r19, -28(r1)
	l.jr	r9
