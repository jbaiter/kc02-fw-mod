OUTPUT_FORMAT("elf32-or1kle")
OUTPUT_ARCH(or1k)
ENTRY(test_entry)
CACHE_clean = 0x0202B058;
MEMORY { CODE (rx) : ORIGIN = 0x020D8100, LENGTH = @CODE_BUDGET@ }
SECTIONS {
 .text : { KEEP(*(.entry)) *(.text .text.*) } > CODE
 .rodata : { *(.rodata .rodata.*) } > CODE
 .data : { *(.data .data.*) } > CODE
 .bss : { *(.bss .bss.*) *(COMMON) } > CODE
 /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
 ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "mutable static data is forbidden")
 ASSERT(. <= ORIGIN(CODE) + LENGTH(CODE), "2 KiB code budget exceeded")
}
