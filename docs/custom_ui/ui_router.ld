/* Pin each piece into the image slot its stock handler occupied. No free space is used:
   the pieces replace the handlers one-for-one. 100-byte slots, asserted. */
OUTPUT_FORMAT("elf32-or1kle")
OUTPUT_ARCH(or1k)
ENTRY(camera_power_router)
SECTIONS {
  . = 0x02009F08;
  .slot_power : { *(.slot_power) }
  ASSERT(. <= 0x02009F08 + 100, "camera POWER slot overflowed (100 bytes)")

  . = 0x02016C14;
  .slot_settings : { *(.slot_settings) }
  ASSERT(. <= 0x02016C14 + 100, "settings slot overflowed (100 bytes)")

  . = 0x0200BD4C;
  .slot_playback : { *(.slot_playback) }
  ASSERT(. <= 0x0200BD4C + 100, "playback slot overflowed (100 bytes)")

  /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) *(.rodata*) *(.data*) *(.bss*) }
}
