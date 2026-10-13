OUTPUT_FORMAT("elf32-or1kle")
OUTPUT_ARCH(or1k)
ENTRY(camera_mode_enter)
fw_encoder_init = 0x02040384;
fw_sensor_adjust = 0x020487a4;
fw_record_config = 0x0205609c;
fw_frame_mode = 0x0204606c;
fw_color_preset = 0x020588c0;
fw_crop_start = 0x020049f8;
fw_ui_state = 0x02004944;
fw_layer_init = 0x02004588;
fw_layer_uninit = 0x020041b0;
fw_window_open = 0x02012304;
fw_delay = 0x020218b4;
fw_csi_enable = 0x02034ee4;
fw_frame_get = 0x02022720;
fw_frame_submit = 0x02039e44;
fw_frame_rearm = 0x0203a84c;
fw_effect = 0x02022740;
fw_edge = 0x02022434;
fw_group3_effect = 0x020460b4;
fw_child = 0x02025154;
fw_set_resid = 0x0202990c;
fw_visible = 0x0202984c;
fw_present = 0x0205b8c4;
fw_mode_request = 0x02008108;
fw_init_camera_name = 0x020075e8;
fw_load_audio = 0x02007744;
fw_audio_buffer = 0x02007410;
fw_glyphs_init = 0x02077cf4;
fw_glyphs_free = 0x02078188;
fw_exit_stub = 0x020568d4;
fw_record_reset = 0x02054f0c;
fw_time = 0x02021894;
fw_board_ioctl = 0x02002a4c;
fw_config_get = 0x02003e34;
fw_setting_value = 0x020064b4;
fw_template_load = 0x020454fc;
fw_template_resize = 0x020459ac;
fw_rtc_get = 0x0203bae0;
fw_rtc_string = 0x0203bb50;
fw_audio_play = 0x020071c4;
fw_audio_status = 0x020524a0;
fw_service = 0x0202bc7c;
fw_queue_post = 0x02021648;
fw_space_check = 0x0200117c;
fw_filename_create = 0x02001da0;
fw_file_add = 0x020017a0;
fw_sd_lock = 0x0203ce04;
fw_sd_unlock = 0x0203ce44;
fw_sd_stop = 0x0203c6e8;
fw_open = 0x0206c5b8;
fw_close = 0x0206c680;
fw_file_size = 0x0206caf8;
fw_unlink = 0x02072b18;
fw_account_space = 0x0200d344;
fw_manager_service = 0x02000848;
fw_file_path = 0x02002138;
fw_print_file = 0x0205adf0;
fw_print_image = 0x0205b0fc;
fw_watermark_close = 0x0203defc;
fw_watermark_open = 0x0203e014;
fw_watermark_position = 0x0203e410;
fw_encode_flag = 0x02056188;
fw_encode_file = 0x02076f98;
fw_capture_size_snapshot = 0x02040478;
fw_encoder_mem_init = 0x020401a8;
fw_csi_resolution = 0x020381fc;
fw_encode_start = 0x02040c6c;
fw_alloc = 0x0203e5c4;
fw_free = 0x0203e694;
fw_encoded_get = 0x020416cc;
fw_encoded_release = 0x0204183c;
fw_watermark_update = 0x02054bfc;
fw_cache_writeback = 0x0202b058;
fw_cache_flush = 0x0202b0d4;
fw_jpeg_decode = 0x02056368;
fw_decode_resolution = 0x0205681c;
fw_image_transform = 0x02046150;
fw_watermark_stamp = 0x02077ef0;
fw_encoder_uninit = 0x020403ec;
fw_copy = 0x020782b0;
SECTIONS {
 . = 0x02300000;
 .text : { *(.text .text.*) }
 . = ALIGN(32);
 .rodata : { *(.rodata .rodata.*) }
 . = ALIGN(32);
 .data : { *(.data .data.*) }
 . = ALIGN(32);
 .bss : { *(.bss .bss.*) *(COMMON) }
 /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
