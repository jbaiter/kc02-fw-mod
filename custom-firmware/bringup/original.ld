/* Original KC02 only. Preserve the factory BLDR/header/SDRAM initializer.
 * Bootstrap LMA is the stock app load address; entry is its actual handoff.
 */
OUTPUT_FORMAT("elf32-or1kle")
ENTRY(_start)
SECTIONS {
  .boot 0x02000000 : { KEEP(*(.boot)) }
  ASSERT(_start == 0x0200046c, "incorrect factory entry")
  ASSERT(SIZEOF(.boot) < 0x2000, "bootstrap overlaps workload")
  .vectors 0x02000800 : { __vectors = .; KEEP(*(.vectors)) }
  ASSERT(__vectors == 0x02000800, "vector base must be fixed")
  ASSERT(SIZEOF(.vectors) == 0x100, "vector table must be 16 slots")
  ASSERT(SIZEOF(.boot) <= 0x800, "bootstrap overlaps the vector table")
  .probe 0x02002000 : { KEEP(*(.probe.text)) }
  ASSERT(SIZEOF(.probe) < 0x800, "workload overlaps observations")
  .observations 0x02002800 : { KEEP(*(.probe.data)) }
  .monitor 0x027c0000 : AT(0x02003000) {
    __monitor_start = .;
    *(.text*) *(.rodata*) *(.data*)
    . = ALIGN(4); __monitor_end = .;
  }
  __monitor_load = LOADADDR(.monitor);
  .bss (NOLOAD) : {
    . = ALIGN(32); __bss_start = .; *(.bss*) *(COMMON)
    . = ALIGN(4); __bss_end = .;
  }
  __monitor_stack = 0x027ffff0;
  __application_stack = 0x027b0000;
  ASSERT(__bss_end <= __monitor_stack - 32768, "monitor/stack overlap")
  ASSERT(LOADADDR(.monitor) + SIZEOF(.monitor) <= 0x020c8800, "factory load span exceeded")
  /DISCARD/ : { *(.comment) *(.eh_frame*) }
}
