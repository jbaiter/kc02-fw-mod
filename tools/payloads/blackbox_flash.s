/*
 * blackbox_flash.s -- SPI flash -> USB streamer for AX3292 KC02s.
 *
 * Same staging/entry convention as blackbox_read.s.  MscCmd.DataAddr is the
 * physical flash offset and MscCmd.Residue is the byte count.  It calls the
 * firmware's SPI reader at SPI_READ, materialised by the movhi/ori pair below
 * (patched by the host tool after the reader is located by signature), with
 * (offset, 0x8040, chunk), then streams the fixed TX endpoint buffer back with
 * the firmware's own bulk-TX function pointer.
 *
 * Known SPI reader addresses:
 *   original KC02 : 0x0203D38C
 *   newer build   : 0x02037404
 *
 * Assembled with:  or1k-elf-as -o blackbox_flash.o blackbox_flash.s
 *                  or1k-elf-objcopy -O binary blackbox_flash.o blackbox_flash.bin
 */
	.text
	.global _start
_start:
	l.sw	-4(r1), r9
	l.sw	-8(r1), r2
	l.sw	-12(r1), r14
	l.sw	-16(r1), r15
	l.sw	-20(r1), r16
	l.sw	-24(r1), r17
	l.sw	-28(r1), r18
	l.sw	-32(r1), r19
	l.sw	-36(r1), r20
	l.addi	r1, r1, -36
	l.ori	r14, r3, 0
	l.ori	r2, r4, 0
	l.lwz	r19, 56(r14)       /* flash offset = DataAddr */
	l.lwz	r15, 36(r14)       /* len = Residue */
	l.lwz	r18, 36(r2)        /* fp_usb_bulk_tx */
	l.lbz	r16, 1(r2)         /* ep */
	l.movhi	r17, 0x203         /* SPI_READ high half (tool-patched) */
	l.ori	r17, r17, 0x7404   /* SPI_READ low half  (tool-patched) */
loop:
	l.sfeqi	r15, 0
	l.bf	done
	l.ori	r20, r0, 512
	l.sfgtu	r15, r20
	l.bf	have
	l.ori	r20, r15, 0         /* chunk = min(len, 512) */
have:
	l.ori	r3, r19, 0
	l.movhi	r4, 0x0
	l.ori	r4, r4, 0x8040
	l.ori	r5, r20, 0
	l.jalr	r17                /* spi_read(offset, 0x8040, chunk) */
	l.ori	r3, r16, 0
	l.movhi	r4, 0x0
	l.ori	r4, r4, 0x8040
	l.ori	r5, r20, 0
	l.add	r19, r19, r20
	l.sub	r15, r15, r20
	l.jalr	r18                /* tx(ep, 0x8040, chunk) */
	l.j	loop
done:
	l.addi	r1, r1, 36
	l.lwz	r9, -4(r1)
	l.lwz	r2, -8(r1)
	l.lwz	r14, -12(r1)
	l.lwz	r15, -16(r1)
	l.lwz	r16, -20(r1)
	l.lwz	r17, -24(r1)
	l.lwz	r18, -28(r1)
	l.lwz	r19, -32(r1)
	l.lwz	r20, -36(r1)
	l.jr	r9
