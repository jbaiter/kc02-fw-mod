/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009890 Name: video_key_next
 */

/* Track2 evidence-based naming (likely, names_02.tsv): Symmetric key handler that increments
   app_context.b8 and walks the preview/record state machine forwards, sharing
   record_config_set/FUN_ram_0204606c control.  [was FUN_ram_02009890] */

undefined4 video_key_next(undefined4 param_1,int param_2,int *param_3)

{
  byte bVar1;
  sbyte sVar2;
  undefined4 uVar3;
  
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
    sVar2 = app_context.b8 + 1;
    app_context.b8 = app_context.b8 + 1;
    if ('\x0f' < (char)sVar2) {
      app_context.b8 = 0xf;
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
    if (app_context.b5b != 0) {
      if (app_context.b5f == 0) {
        app_context.b5b = app_context.b5b - 1;
      }
      else {
        app_context.b5b = app_context.b59;
      }
      goto LAB_ram_02009a74;
    }
    app_context.b59 = 3;
    mjpeg_set_frame_mode(1);
    record_config_set(8,1);
    app_context.b5e = 8;
    uVar3 = uRam020c2570;
  }
  else {
    if (app_context.b59 == 1) {
      if (app_context.b5a != 0) {
        app_context.b5a = app_context.b5a - 1;
        isp_set_ccm_preset();
        bVar1 = app_context.b5a;
        if (app_context.b5a == 0) {
          isp_set_ccm_preset(0);
          app_context.b59 = bVar1;
          if (app_context.b5f == 0) {
            app_context.b5b = 8;
          }
          else {
            app_context.b5b = 7;
          }
        }
      }
      goto LAB_ram_02009a74;
    }
    if (app_context.b59 == 2) {
      if (app_context.b5d == 0) {
        app_context.b59 = 1;
        app_context.b5a = 5;
        isp_set_ccm_preset(5);
      }
      else {
        app_context.b5d = app_context.b5d - 1;
      }
      goto LAB_ram_02009a74;
    }
    if (app_context.b59 != 3) goto LAB_ram_02009a74;
    if (app_context.b5e == 0) {
      record_config_set(8,0);
      mjpeg_set_frame_mode(0);
      app_context.b5d = 7;
      app_context.b59 = 2;
      goto LAB_ram_02009a74;
    }
    app_context.b5e = app_context.b5e - 1;
    uVar3 = *(undefined4 *)(&DAT_ram_020c2550 + (uint)app_context.b5e * 4);
  }
  func_0x020454fc(uVar3);
LAB_ram_02009a74:
  if (((app_context.b59 - 2 < 2) || (app_context.b5b - 7 < 2)) && (app_context.b8 != 0)) {
    app_context.b8 = 0;
    mjpeg_start_recording(0,register0x00000401);
    func_0x020459ac(*(undefined4 *)(&DAT_ram_020c2550 + (uint)app_context.b5e * 4));
    ui_show_number_label(param_1);
  }
  FUN_ram_02008d98(param_1);
  return 0;
}

