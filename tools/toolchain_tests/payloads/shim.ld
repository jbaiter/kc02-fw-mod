OUTPUT_FORMAT("elf32-or1kle")
OUTPUT_ARCH(or1k)
ENTRY(test_entry)
SPI_read = 0x0203D38C;
MEMORY { CODE (rx) : ORIGIN = 0x020D8000, LENGTH = 128 }
SECTIONS {
 .text : { KEEP(*(.entry)) *(.text .text.*) } > CODE
 .rodata : { *(.rodata .rodata.*) } > CODE
 /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
 ASSERT(. <= ORIGIN(CODE) + LENGTH(CODE), "shim budget exceeded")
}
