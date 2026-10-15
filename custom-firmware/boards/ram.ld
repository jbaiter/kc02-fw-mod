/* RAM-only development image. Not a bootloader or flash/update image. */
OUTPUT_FORMAT("elf32-or1kle")
ENTRY(_start)
MEMORY { ram (rwx) : ORIGIN = 0x02000000, LENGTH = 2M }
SECTIONS {
  .text : { KEEP(*(.text.start)) *(.text*) } > ram
  .rodata : { *(.rodata*) } > ram
  .data : { *(.data*) } > ram
  .bss (NOLOAD) : {
    . = ALIGN(4); __bss_start = .; *(.bss*) *(COMMON)
    . = ALIGN(4); __bss_end = .;
  } > ram
  __stack_top = ORIGIN(ram) + LENGTH(ram) - 16;
  ASSERT(__bss_end < __stack_top - 16384, "RAM budget exceeded")
  /DISCARD/ : { *(.comment) *(.eh_frame*) }
}
