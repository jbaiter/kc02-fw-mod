/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020082f4 Name: mode_photo_enter
 */

/* mode 3 Photo Encode (camera) */

void mode_photo_enter(void)

{
  uint p0;
  uint p1;
  uint *p2;
  uint p3;
  
  DAT_ram_020ccd8c = 1;
  hal_mjpegEncodeInit();
  exposal_adj(p0,p1,p2,p3);
  record_config_set(8,0,register0x00000401);
  mjpeg_set_frame_mode(0);
  app_context.b5e = 0;
  app_context.b5a = 0;
  isp_set_ccm_preset(0);
  app_context.b8 = 0;
  mjpeg_start_recording(0,0);
  app_context.b7 = 0;
  ui_state_apply(0);
  dispLayerInit(0);
  app_context.b58 = 1;
  app_context.b59 = 0;
  app_context.b5b = 0;
  win_open(&mode3_keymap_descriptor,0);
  XOSTimeDly(0x32);
  ax32xx_csiEnable(1);
  XOSTimeDly(0x32);
  return;
}

