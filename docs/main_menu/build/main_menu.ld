OUTPUT_FORMAT("elf32-or1kle")
OUTPUT_ARCH(or1k)
ENTRY(main_menu_mode_enter)
menu_fw_csi_enable = 0x02034ee4;
menu_fw_ui_state = 0x02004944;
menu_fw_window_open = 0x02012304;
menu_fw_frame_get = 0x020047f8;
menu_fw_lcd_resolution = 0x0203a5c8;
menu_fw_frame_config = 0x02044ce0;
menu_fw_copy = 0x020782b0;
menu_fw_blit = 0x02022060;
menu_fw_cache_flush = 0x0202b0d4;
menu_fw_frame_show = 0x020047b4;
menu_fw_child = 0x02025154;
menu_fw_visible = 0x0202984c;
menu_fw_free = 0x0203e694;
menu_fw_alloc = 0x0203e5c4;
menu_fw_decode_background = 0x02021a78;
menu_fw_decode_icon = 0x02021d00;
menu_fw_time = 0x02021894;
menu_fw_present = 0x0205b8c4;
menu_fw_mode_request = 0x02008108;
SECTIONS {
 . = 0x02310000;
 .text : { *(.text .text.*) }
 . = ALIGN(32);
 .rodata : { *(.rodata .rodata.*) }
 . = ALIGN(32);
 .data : { *(.data .data.*) }
 . = ALIGN(32);
 .bss : { *(.bss .bss.*) *(COMMON) }
 /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
