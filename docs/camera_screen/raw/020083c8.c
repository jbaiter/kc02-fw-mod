/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020083c8 Name: mode_photo_exit
 */

/* pipeline map */

void mode_photo_exit(void)

{
  int iVar1;
  
  DAT_ram_020ccd8c = 0;
  app_context.b58 = 0;
  app_context.b59 = 0;
  app_context.b5b = 0;
  record_config_set(8,0,&stack0x00000000);
  mjpeg_set_frame_mode(0);
  app_context.b5a = 0;
  isp_set_ccm_preset(0);
  if (app_context.b5c == 0) {
    app_context.b5c = 1;
    iVar1 = FUN_ram_02022720();
    if (iVar1 != 0) {
      app_context.b5c = 2;
      hal_lcdDealBuffer(iVar1);
      lcd_commit_video_frame();
    }
  }
  ui_state_apply(app_context.b7);
  dispLayerUninit(0);
  unk_020568d4_stub();
  record_module_reset();
  if (app_context.b4 == 3) {
    app_context.b4 = 4;
  }
  return;
}

