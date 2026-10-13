/* artprint links .bss inside the verified RAM code window. The payload initialises
   every mutable global at entry, so zero-fill at load is not required; objcopy omits
   .bss from the .bin, and the footprint assertion keeps it inside the 5632-byte slot. */
OUTPUT_FORMAT("elf32-or1kle")
OUTPUT_ARCH(or1k)
ENTRY(test_entry)
CACHE_clean = 0x0202B058;
MEMORY { CODE (rwx) : ORIGIN = 0x020D8100, LENGTH = 5632 }
SECTIONS {
 .text : { KEEP(*(.entry)) *(.text .text.*) } > CODE
 .rodata : { *(.rodata .rodata.*) } > CODE
 .data : { *(.data .data.*) } > CODE
 .bss : { *(.bss .bss.*) *(COMMON) } > CODE
 /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
 ASSERT(. <= ORIGIN(CODE) + LENGTH(CODE), "code window exceeded")
}
