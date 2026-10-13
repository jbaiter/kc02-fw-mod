/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009568 Name: video_key_prev
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* Track2 evidence-based naming (likely, names_02.tsv): Key handler that decrements the numeric
   setting app_context.b8 and walks the preview/record state machine (b59/b5b/b5a/b5d/b5e)
   backwards.  [was FUN_ram_02009568] */

undefined4 video_key_prev(undefined4 param_1,int param_2,int *param_3)

{
  sbyte sVar1;
  undefined4 uVar2;
  
  if (param_2 != 1) {
    return 0;
  }
  if (*param_3 != 2) {
    if (*param_3 != 1) {
      return 0;
    }
    if (app_context.b59 - 2 < 2) {
      return 0;
    }
    if (app_context.b5b - 7 < 2) {
      return 0;
    }
    sVar1 = app_context.b8 - 1;
    app_context.b8 = app_context.b8 - 1;
    if (200 < (char)sVar1) {
      app_context.b8 = 0;
    }
    mjpeg_start_recording(app_context.b8,0,&stack0x00000000);
    if (app_context.b59 == 3) {
      func_0x020459ac(*(undefined4 *)(&DAT_ram_020c2550 + (uint)app_context.b5e * 4));
    }
    app_context.b6d = 1;
    ui_show_record_indicator(param_1);
    ui_show_number_label(param_1);
    return 0;
  }
  if (app_context.b6d == 1) {
    DAT_ram_020ccda4 = XOSTimeGet();
    app_context.b6d = 2;
    ui_show_record_indicator(param_1);
    func_0x02007410(uRam020ccda0,uRam020ccd9c);
    return 0;
  }
  if (app_context.b59 == 0) {
    if (app_context.b5b < 8) {
      if (app_context.b5f == 0) {
        app_context.b5b = app_context.b5b + 1;
        goto LAB_ram_02009744;
      }
      if (app_context.b5b != 7) {
        app_context.b5b = 7;
        goto LAB_ram_02009744;
      }
    }
    app_context.b5b = 0;
    app_context.b59 = 1;
    app_context.b5a = 1;
    sVar1 = 1;
  }
  else {
    if (app_context.b59 != 1) {
      if (app_context.b59 == 2) {
        if (app_context.b5d < 7) {
          app_context.b5d = app_context.b5d + 1;
          goto LAB_ram_02009744;
        }
        app_context.b59 = 3;
        mjpeg_set_frame_mode(1);
        record_config_set(8,1);
        app_context.b5e = 0;
        uVar2 = _DAT_ram_020c2550;
      }
      else {
        if (app_context.b59 != 3) goto LAB_ram_02009744;
        if (7 < app_context.b5e) {
          record_config_set(8,0);
          mjpeg_set_frame_mode(0);
          app_context.b59 = 0;
          app_context.b5b = 0;
          goto LAB_ram_02009744;
        }
        app_context.b5e = app_context.b5e + 1;
        uVar2 = *(undefined4 *)(&DAT_ram_020c2550 + (uint)app_context.b5e * 4);
      }
      func_0x020454fc(uVar2);
      goto LAB_ram_02009744;
    }
    if (4 < app_context.b5a) {
      app_context.b5a = 0;
      isp_set_ccm_preset(0);
      app_context.b5d = 0;
      app_context.b59 = 2;
      goto LAB_ram_02009744;
    }
    sVar1 = app_context.b5a + 1;
    app_context.b5a = app_context.b5a + 1;
  }
  isp_set_ccm_preset(sVar1);
LAB_ram_02009744:
  if (((app_context.b59 - 2 < 2) || (app_context.b5b - 7 < 2)) && (app_context.b8 != 0)) {
    app_context.b8 = 0;
    mjpeg_start_recording(0,register0x00000401);
    func_0x020459ac(*(undefined4 *)(&DAT_ram_020c2550 + (uint)app_context.b5e * 4));
    ui_show_number_label(param_1);
  }
  FUN_ram_02008d98(param_1);
  return 0;
}

