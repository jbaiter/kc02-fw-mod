/* KC02 camera-screen decompilation. NOT recovered vendor source or buildable C.
 * Firmware SHA256: 218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5
 * Current Ghidra labels/prototypes may be wrong; see CAMERA_SCREEN.md.
 */

/* ==================== 0x020082f4 ==================== */
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


/* ==================== 0x020083c8 ==================== */
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


/* ==================== 0x020084a0 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020084a0 Name: mode_photo_tick
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* pipeline map */

void mode_photo_tick(void)

{
  int iVar1;
  
  ui_record_indicator_timeout(_DAT_ram_020c2b48,&stack0x00000000);
  if (app_context.b5c == 0) {
    app_context.b5c = 1;
    iVar1 = FUN_ram_02022720();
    if (iVar1 != 0) {
      if (app_context.b59 == 2) {
        photo_apply_effect(*(undefined4 *)(iVar1 + 4),*(undefined4 *)(iVar1 + 8),
                           *(undefined2 *)(iVar1 + 0x20),*(undefined2 *)(iVar1 + 0x22));
      }
      else if (app_context.b59 == 3) {
        FUN_ram_020460b4(*(undefined4 *)(iVar1 + 4));
      }
      app_context.b5c = 2;
      hal_lcdDealBuffer(iVar1);
      lcd_commit_video_frame();
    }
  }
  return;
}


/* ==================== 0x02008550 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008550 Name: FUN_ram_02008550
 */

int FUN_ram_02008550(undefined4 *param_1)

{
  int iVar1;
  char *pcVar2;
  
  if (app_context.b4 == 4) {
    iVar1 = managerSpaceCheck(app_context._56_4_,s_PHOTO__ram_0207b859,0x1400);
    if ((iVar1 < 0) ||
       (pcVar2 = managerFileNameCreater(app_context._56_4_,&DAT_ram_0207b870,s_PHOTO__ram_0207b859),
       pcVar2 == (char *)0x0)) {
      app_context.b4 = 3;
      iVar1 = -2;
      XMsgQPost((undefined *)app_context._116_4_,(undefined *)0x1);
    }
    else {
      hal_sdLock();
      iVar1 = open(pcVar2,7,register0x00000401);
      if (param_1 != (undefined4 *)0x0) {
        *param_1 = pcVar2;
      }
    }
  }
  else {
    iVar1 = -1;
    XMsgQPost((undefined *)app_context._116_4_,(undefined *)0x1);
  }
  return iVar1;
}


/* ==================== 0x02008630 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008630 Name: FUN_ram_02008630
 */

int FUN_ram_02008630(void)

{
  undefined4 uVar1;
  undefined1 uVar2;
  int iVar3;
  uint uVar4;
  int p0;
  uint uVar5;
  uint uVar6;
  undefined *puStack_30;
  
  hal_watermarkClose((int)(sbyte)JPEG_ram_020df023._16734_1_);
  iVar3 = hal_watermarkOpen();
  JPEG_ram_020df023._16734_1_ = SUB41(iVar3,0);
  uVar2 = setting_map_config_index(0xe);
  uVar4 = setting_map_config_index(0x18);
  uVar6 = uVar4 >> 0x10;
  iVar3 = func_0x02056188();
  hal_watermarkBPos_Adjust(0x1e0);
  uVar1 = 3;
  if (((0x500 < uVar6) && (uVar1 = 4, 0xa00 < uVar6)) && (uVar1 = 6, 0xc80 < uVar6)) {
    uVar1 = 0;
  }
  p0 = FUN_ram_02008550(0,&puStack_30);
  uVar5 = 0;
  if (-1 < p0) {
    uVar5 = 1;
    app_context.b58 = 0;
    DAT_ram_020ccd90 = 1;
    iVar3 = func_0x02076f98(puStack_30,p0,uVar6,uVar4 & 0xffff,uVar1,uVar2,(uint)-iVar3 >> 0x1f,0x14
                            ,0x294,0,0);
    DAT_ram_020ccd90 = 0;
    app_context.b58 = 1;
    if (iVar3 < 0) {
      close(p0);
      f_unlink(puStack_30);
      uVar5 = 0;
    }
    else {
      uVar1 = fs_size(p0);
      vr_account_free_space(0xffffffff,uVar1);
      close(p0);
      managerAddFile(app_context._56_4_,s_PHOTO__ram_0207b859,puStack_30 + 6);
      hal_sdStop();
    }
  }
  hal_sdUnlock();
  return -(uVar5 ^ 1);
}


/* ==================== 0x020087e8 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020087e8 Name: photo_encode_frame
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* photo JPEG encoder: closes/reopens watermark (hal_watermarkClose), quality tier from width
   (>0x500->3, >0xA00->4, >0xC80->6), 512 KiB output buffer, 2 s timeout. Source for photo save AND
   printing. */

undefined4 photo_encode_frame(undefined4 *param_1,uint *param_2)

{
  undefined4 uVar1;
  sbyte sVar2;
  int iVar3;
  uint uVar4;
  int iVar5;
  undefined *puVar6;
  undefined *p0;
  uint uVar7;
  uint uVar8;
  ushort uStack_5c;
  ushort uStack_5a;
  undefined1 auStack_58 [2];
  undefined1 auStack_56 [2];
  undefined4 uStack_54;
  uint uStack_50;
  undefined1 auStack_4c [4];
  undefined1 auStack_48 [4];
  undefined1 uStack_44;
  undefined1 uStack_43;
  uint uStack_40;
  int iStack_3c;
  undefined2 uStack_38;
  undefined *puStack_34;
  ushort uStack_30;
  ushort uStack_2e;
  
  hal_watermarkClose((int)(sbyte)JPEG_ram_020df023._16734_1_);
  iVar3 = hal_watermarkOpen();
  JPEG_ram_020df023._16734_1_ = SUB41(iVar3,0);
  sVar2 = setting_map_config_index(0xe);
  uVar4 = setting_map_config_index(0x18);
  uVar8 = uVar4 >> 0x10;
  iVar3 = func_0x02056188();
  func_0x02040478(uVar8,uVar4 & 0xffff);
  iVar5 = hal_mjpegMemInit();
  uVar1 = 0xffffffff;
  if (-1 < iVar5) {
    uVar1 = 3;
    if (((0x500 < uVar8) && (uVar1 = 4, 0xa00 < uVar8)) && (uVar1 = 6, 0xc80 < uVar8)) {
      uVar1 = 0;
    }
    hal_csiResolutionGet(&uStack_5c,&uStack_5a);
    func_0x02040c6c(uStack_5c,uStack_5a,uVar1,(uint)-iVar3 >> 0x1f,0);
    puVar6 = hal_sysMemMalloc(0x80000,0x40);
    iVar5 = XOSTimeGet();
    do {
      p0 = (undefined *)mjpeg_get_frame(0,&uStack_54,&uStack_50,auStack_4c,auStack_48);
      if (p0 != (undefined *)0x0) {
        if (puVar6 != (undefined *)0x0) {
          memcpy(puVar6,p0,uStack_50);
          hal_mjpegRawDataSet(p0);
        }
        break;
      }
      uVar7 = XOSTimeGet();
    } while (uVar7 <= iVar5 + 2000U);
    if (sVar2 != 0) {
      video_watermark_update(uVar8,uVar4 & 0xffff,0);
    }
    _DAT_ram_020d4214 = _DAT_ram_020d4210 + (uint)_DAT_ram_020d4204 * (uint)_DAT_ram_020d4206;
    ax32xx_sysDcacheWback((uint)puVar6,uStack_50);
    ax32xx_csiEnable(1);
    ui_state_apply(7);
    XOSTimeDly(0x14);
    XOSTimeGet();
    uStack_30 = _DAT_ram_020d4204;
    uStack_2e = _DAT_ram_020d4206;
    uStack_40 = _DAT_ram_020d4210;
    iStack_3c = _DAT_ram_020d4214;
    uStack_38 = 2;
    uStack_43 = 1;
    puStack_34 = puVar6;
    iVar5 = jpeg_show_or_save(&uStack_44);
    if (-1 < iVar5) {
      XOSTimeGet();
      mjpeg_get_decode_resolution(auStack_58,auStack_56);
    }
    ax32xx_csiEnable(1);
    ui_state_apply(app_context.b7);
    hal_sysMemFree(puVar6);
    if (DAT_ram_020d41fa == 1) {
      FUN_ram_02046150(_DAT_ram_020d4210);
    }
    if (app_context.b59 == 2) {
      photo_apply_effect(_DAT_ram_020d4210,_DAT_ram_020d4214,_DAT_ram_020d4204,_DAT_ram_020d4206);
    }
    else if ((app_context.b59 == 0) && (app_context.b5b - 7 < 2)) {
      photo_effect_edge_detect
                (_DAT_ram_020d4210,_DAT_ram_020d4214,_DAT_ram_020d4204,_DAT_ram_020d4206,
                 -(app_context.b5b ^ 7) >> 0x1f);
    }
    if (sVar2 != 0) {
      func_0x02077ef0(_DAT_ram_020d4210,0x14,0x280,0);
    }
    ax32xx_sysDcacheWback
              (_DAT_ram_020d4210,(int)((uint)_DAT_ram_020d4204 * (uint)_DAT_ram_020d4206 * 3) >> 1);
    func_0x02040c6c(uStack_5c,uStack_5a,uVar1,sVar2,(uint)-iVar3 >> 0x1f,1);
    iVar3 = XOSTimeGet();
    do {
      puVar6 = (undefined *)mjpeg_get_frame(p0,&uStack_54,&uStack_50,auStack_4c,auStack_48);
      if (puVar6 != (undefined *)0x0) {
        hal_mjpegRawDataSet(puVar6);
        puVar6 = hal_sysMemMalloc(uStack_50,0x20);
        if (puVar6 == (undefined *)0x0) {
          uVar1 = 0xfffffffe;
        }
        else {
          memcpy(puVar6,uStack_54,uStack_50);
          ax32xx_sysDcacheFlush((uint)puVar6,uStack_50);
          *param_1 = puVar6;
          *param_2 = uStack_50;
          uVar1 = 0;
        }
        goto LAB_ram_02008b98;
      }
      uVar4 = XOSTimeGet();
      p0 = (undefined *)0x0;
    } while (uVar4 <= iVar3 + 2000U);
    uVar1 = 0xffffffff;
LAB_ram_02008b98:
    hal_mjpegEncodeUninit();
  }
  return uVar1;
}


/* ==================== 0x02008bd4 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008bd4 Name: FUN_ram_02008bd4
 */

undefined4 FUN_ram_02008bd4(void)

{
  return 0;
}


/* ==================== 0x02008bec ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008bec Name: FUN_ram_02008bec
 */

void FUN_ram_02008bec(undefined4 param_1)

{
  sbyte sVar1;
  int iVar2;
  undefined4 uVar3;
  
  if (app_context.b2 != 0) {
    sVar1 = app_context.b6a + 1;
    app_context.b6a = app_context.b6a + 1;
    if ('\x03' < (char)sVar1) {
      app_context.b6a = 0;
    }
    uVar3 = win_find_child_by_id(param_1,10,&stack0x00000000);
    iVar2 = app_context.b6a + 0x80000022;
    goto LAB_ram_02008d08;
  }
  if (app_context.b0 != 4) {
    if (app_context.b0 == 3) {
      uVar3 = win_find_child_by_id(10,&stack0x00000000);
      iVar2 = -0x7fffffdc;
      goto LAB_ram_02008d08;
    }
    if (app_context.b0 == 2) {
      uVar3 = win_find_child_by_id(10,&stack0x00000000);
      iVar2 = -0x7fffffdd;
      goto LAB_ram_02008d08;
    }
    if (app_context.b0 == 1) {
      uVar3 = win_find_child_by_id(10,&stack0x00000000);
      iVar2 = -0x7fffffde;
      goto LAB_ram_02008d08;
    }
    if (app_context.b0 == 0) {
      uVar3 = win_find_child_by_id(10,&stack0x00000000);
      iVar2 = -0x7fffffdf;
      goto LAB_ram_02008d08;
    }
  }
  uVar3 = win_find_child_by_id(param_1,10,&stack0x00000000);
  iVar2 = -0x7fffffdb;
LAB_ram_02008d08:
  winSetResid(uVar3,iVar2);
  uVar3 = win_find_child_by_id(param_1,10,register0x00000401);
  winSetVisible(uVar3,1);
  return;
}


/* ==================== 0x02008d3c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008d3c Name: FUN_ram_02008d3c
 */

undefined4 FUN_ram_02008d3c(void)

{
  if (app_context.b2 == 0) {
    FUN_ram_02008bec(&stack0x00000000);
  }
  return 0;
}


/* ==================== 0x02008d74 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008d74 Name: FUN_ram_02008d74
 */

undefined4 FUN_ram_02008d74(void)

{
  FUN_ram_02008bec(&stack0x00000000);
  return 0;
}


/* ==================== 0x02008d98 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008d98 Name: FUN_ram_02008d98
 */

void FUN_ram_02008d98(undefined4 param_1)

{
  byte bVar1;
  undefined4 uVar2;
  undefined4 uVar3;
  
  bVar1 = app_context.b5b;
  if (app_context.b5b != 0) {
    if (app_context.b5b != 1) {
      if (app_context.b5b == 2) {
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000040;
      }
      else if (app_context.b5b == 3) {
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000046;
      }
      else if (app_context.b5b == 4) {
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000047;
      }
      else if (app_context.b5b == 5) {
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000045;
      }
      else if (app_context.b5b == 6) {
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000048;
      }
      else if (app_context.b5b == 7) {
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000043;
      }
      else {
        if (app_context.b5b != 8) {
          return;
        }
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000044;
      }
      winSetResid(uVar3,uVar2);
      uVar3 = win_find_child_by_id(param_1,0xf,register0x00000401);
      bVar1 = 1;
      goto LAB_ram_02008ef0;
    }
    uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
    winSetResid(uVar3,0x8000003f);
  }
  uVar3 = win_find_child_by_id(param_1,0xf,register0x00000401);
LAB_ram_02008ef0:
  winSetVisible(uVar3,bVar1);
  return;
}


/* ==================== 0x02008f0c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008f0c Name: FUN_ram_02008f0c
 */

void FUN_ram_02008f0c(undefined4 param_1)

{
  undefined4 uVar1;
  
  if (iRam020ccd98 == 0) {
    uVar1 = win_find_child_by_id(0x19,&stack0x00000000);
    winSetResid(uVar1,0x8000002a);
    uVar1 = win_find_child_by_id(param_1,0x1a);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,0x19);
    winSetVisible(uVar1,1);
  }
  if (iRam020ccd98 == 2) {
    uVar1 = win_find_child_by_id(param_1,0x19,register0x00000401);
    winSetResid(uVar1,0x8000002b);
    uVar1 = win_find_child_by_id(param_1,0x1a);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,0x19);
    winSetVisible(uVar1,1);
  }
  if (iRam020ccd98 == 1) {
    uVar1 = win_find_child_by_id(param_1,0x1a,register0x00000401);
    winSetResid(uVar1,0x80000026);
    uVar1 = win_find_child_by_id(param_1,0x19);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,0x1a);
    winSetVisible(uVar1,1);
  }
  return;
}


/* ==================== 0x02009050 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009050 Name: ui_show_number_label
 */

/* Track2 evidence-based naming (likely, names_02.tsv): Formats app_context.b8+1 into a 2-digit
   decimal string, sets it into window 0x16, shows the window and calls video_present(1).  [was
   FUN_ram_02009050] */

void ui_show_number_label(undefined4 param_1)

{
  uint uVar1;
  undefined4 uVar2;
  sbyte local_10;
  sbyte local_f;
  undefined1 local_e;
  
  uVar1 = (char)(app_context.b8 + 1) / 10;
  local_10 = (sbyte)(uVar1 % 10) + 0x30;
  local_f = app_context.b8 + 1 + (sbyte)uVar1 * -10 + 0x30;
  local_e = 0x58;
  uVar2 = win_find_child_by_id(0x16);
  winSetResid(uVar2,&local_10);
  uVar2 = win_find_child_by_id(param_1,0x16);
  winSetVisible(uVar2,1);
  video_present(1);
  return;
}


/* ==================== 0x02009108 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009108 Name: ui_show_rtc_clock
 */

/* Track2 evidence-based naming (likely, names_02.tsv): Reads the RTC via
   hal_rtcTimeGet/hal_rtcTime2String, sets the string into window 0xc and refreshes the screen when
   app_context.b2==1.  [was FUN_ram_02009108] */

undefined4 ui_show_rtc_clock(undefined4 param_1)

{
  undefined4 uVar1;
  undefined *p0;
  char *pcVar2;
  
  uVar1 = win_find_child_by_id(0xc,&stack0x00000000);
  p0 = hal_rtcTimeGet();
  pcVar2 = hal_rtcTime2String(p0);
  winSetResid(uVar1,pcVar2);
  if (app_context.b2 == 1) {
    FUN_ram_02008bec(param_1);
  }
  return 0;
}


/* ==================== 0x0200917c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200917c Name: FUN_ram_0200917c
 */

void FUN_ram_0200917c(undefined4 param_1)

{
  uint uVar1;
  uint uVar2;
  uint uVar3;
  undefined4 uVar4;
  sbyte sStack_20;
  sbyte sStack_1f;
  sbyte sStack_1e;
  sbyte sStack_1d;
  sbyte sStack_1c;
  sbyte sStack_1b;
  
  uVar4 = app_context._76_4_;
  uVar2 = setting_map_config_index(0x18);
  uVar3 = setting_map_config_index(0x18);
  uVar1 = 0;
  if (app_context.b4 == 4) {
    uVar2 = (uVar2 >> 0x10) * (uVar3 & 0xffff);
    uVar1 = 0x78;
    if ((((0xe1000 < uVar2) && (uVar1 = 0x104, 0x384000 < uVar2)) &&
        (uVar1 = 0x154, 0x57e400 < uVar2)) && (uVar1 = 400, 0x7d3e80 < uVar2)) {
      uVar1 = 500;
    }
    uVar1 = (uint)uVar4 / uVar1;
  }
  uVar2 = (uVar1 / 100000) / 10;
  sStack_20 = ((sbyte)(uVar1 / 100000) - ((sbyte)uVar2 * 2 + (sbyte)(uVar2 << 3))) + 0x30;
  uVar2 = (uVar1 / 10000) / 10;
  sStack_1f = ((sbyte)(uVar1 / 10000) - ((sbyte)uVar2 * 2 + (sbyte)(uVar2 << 3))) + 0x30;
  uVar2 = (uVar1 / 1000) / 10;
  sStack_1e = ((sbyte)(uVar1 / 1000) - ((sbyte)uVar2 * 2 + (sbyte)(uVar2 << 3))) + 0x30;
  uVar2 = (uVar1 / 100) / 10;
  sStack_1d = ((sbyte)(uVar1 / 100) - ((sbyte)uVar2 * 2 + (sbyte)(uVar2 << 3))) + 0x30;
  uVar2 = uVar1 / 10;
  sStack_1c = ((sbyte)uVar2 - ((sbyte)(uVar2 / 10) * 2 + (sbyte)(uVar2 / 10 << 3))) + 0x30;
  sStack_1b = ((sbyte)uVar1 - ((sbyte)uVar2 * 2 + (sbyte)(uVar2 << 3))) + 0x30;
  uVar4 = win_find_child_by_id(param_1,0x18);
  winSetResid(uVar4,&sStack_20);
  uVar4 = win_find_child_by_id(param_1,0x18);
  winSetVisible(uVar4,1);
  video_present(1);
  return;
}


/* ==================== 0x02009358 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009358 Name: FUN_ram_02009358
 */

void FUN_ram_02009358(undefined4 param_1,int param_2,sbyte param_3)

{
  undefined4 unaff_r2;
  undefined4 uVar1;
  
  if (param_2 == 0) {
    uVar1 = win_find_child_by_id(0x10,&stack0x00000000);
    winSetVisible(uVar1,0);
    video_present(1);
  }
  else {
    switch(param_3 + -1) {
    case 0:
      unaff_r2 = 0x8000004e;
      break;
    case 1:
      unaff_r2 = 0x80000050;
      break;
    case 2:
      unaff_r2 = 0x80000051;
      break;
    case 3:
      unaff_r2 = 0x80000052;
      break;
    case 4:
      unaff_r2 = 0x80000053;
      break;
    case 5:
      unaff_r2 = 0x80000054;
      break;
    case 6:
      unaff_r2 = 0x80000055;
      break;
    case 7:
      unaff_r2 = 0x80000056;
      break;
    case 8:
      unaff_r2 = 0x80000057;
      break;
    case 9:
      unaff_r2 = 0x8000004f;
    }
    uVar1 = win_find_child_by_id(param_1,0x10,&stack0x00000000);
    winSetResid(uVar1,unaff_r2);
    uVar1 = win_find_child_by_id(param_1,0x10);
    winSetVisible(uVar1,1);
    video_present(1);
    audio_playback_nv(0x29,1);
  }
  return;
}


/* ==================== 0x0200947c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200947c Name: FUN_ram_0200947c
 */

void FUN_ram_0200947c(void)

{
  func_0x02078188(&stack0x00000000);
  uRam020ccd98 = 0;
  UNK_ram_020c8780 = 0;
  return;
}


/* ==================== 0x020094b8 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020094b8 Name: ui_show_record_indicator
 */

/* Track2 evidence-based naming (likely, names_02.tsv): Shows/hides window 0x17 and selects
   indicator resource 0x80000042 or 0x80000041 from the record state app_context.b6d.  [was
   FUN_ram_020094b8] */

void ui_show_record_indicator(undefined4 param_1)

{
  undefined1 uVar1;
  undefined4 uVar2;
  undefined4 uVar3;
  
  if (app_context.b6d == 0) {
    uVar3 = win_find_child_by_id(0x17,&stack0x00000000);
    uVar1 = 0;
    goto LAB_ram_0200954c;
  }
  if (app_context.b6d == 1) {
    uVar3 = win_find_child_by_id(0x17,&stack0x00000000);
    uVar2 = 0x80000042;
LAB_ram_02009534:
    winSetResid(uVar3,uVar2);
  }
  else if (app_context.b6d == 2) {
    uVar3 = win_find_child_by_id(0x17,&stack0x00000000);
    uVar2 = 0x80000041;
    goto LAB_ram_02009534;
  }
  uVar3 = win_find_child_by_id(param_1,0x17,register0x00000401);
  uVar1 = 1;
LAB_ram_0200954c:
  winSetVisible(uVar3,uVar1);
  return;
}


/* ==================== 0x02009568 ==================== */
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


/* ==================== 0x02009890 ==================== */
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


/* ==================== 0x02009bc0 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009bc0 Name: FUN_ram_02009bc0
 */

void FUN_ram_02009bc0(void)

{
  undefined4 uVar1;
  undefined4 uVar2;
  
  if (app_context.b4 == 0) {
    uVar2 = win_find_child_by_id(8,&stack0x00000000);
    uVar1 = 0x8000004d;
  }
  else {
    uVar2 = win_find_child_by_id(8,&stack0x00000000);
    uVar1 = 0x8000004c;
  }
  winSetResid(uVar2,uVar1);
  return;
}


/* ==================== 0x02009c1c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009c1c Name: FUN_ram_02009c1c
 */

void FUN_ram_02009c1c(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  undefined4 uVar1;
  
  uVar1 = win_find_child_by_id(0x13,&stack0x00000000);
  winSetVisible(uVar1,1);
  uVar1 = win_find_child_by_id(param_1,0x14);
  winSetResid(uVar1,param_2);
  uVar1 = win_find_child_by_id(param_1,0x14);
  winSetVisible(uVar1,1);
  uVar1 = win_find_child_by_id(param_1,0x15);
  winSetResid(uVar1,param_3);
  uVar1 = win_find_child_by_id(param_1,0x15);
  winSetVisible(uVar1,1);
  return;
}


/* ==================== 0x02009ccc ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009ccc Name: FUN_ram_02009ccc
 */

void FUN_ram_02009ccc(undefined4 param_1,int param_2,undefined4 param_3,undefined4 param_4)

{
  undefined4 uVar1;
  
  if (param_2 == 0) {
    uVar1 = win_find_child_by_id(0x13,&stack0x00000000);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,0x14);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,0x15);
    winSetVisible(uVar1,0);
  }
  else {
    FUN_ram_02009c1c(param_3,param_4);
  }
  return;
}


/* ==================== 0x02009d5c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009d5c Name: FUN_ram_02009d5c
 */

void FUN_ram_02009d5c(undefined4 param_1)

{
  undefined4 uVar1;
  bool bVar2;
  
  bVar2 = app_context.b5f == 0;
  if (bVar2) {
    uVar1 = win_find_child_by_id(0x11,&stack0x00000000);
  }
  else {
    uVar1 = win_find_child_by_id(0x11,&stack0x00000000);
    winSetResid(uVar1,0x8000004a);
    uVar1 = win_find_child_by_id(param_1,0x11);
  }
  winSetVisible(uVar1,!bVar2);
  return;
}


/* ==================== 0x02009de0 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009de0 Name: FUN_ram_02009de0
 */

undefined4 FUN_ram_02009de0(undefined4 param_1,int param_2,int *param_3)

{
  if ((param_2 == 1) && (*param_3 == 0)) {
    if (app_context.b5f == 0) {
      app_context.b5f = 1;
      if ((app_context.b59 == 0) && (app_context.b5b != 0)) {
        app_context.b5b = 7;
      }
    }
    else {
      app_context.b5f = 0;
    }
    FUN_ram_02009d5c(param_1,&stack0x00000000);
    FUN_ram_02008d98(param_1);
  }
  return 0;
}


/* ==================== 0x02009e70 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009e70 Name: FUN_ram_02009e70
 */

undefined4 FUN_ram_02009e70(undefined4 param_1)

{
  undefined4 uVar1;
  
  FUN_ram_02009bc0(&stack0x00000000);
  FUN_ram_0200917c(param_1);
  if (app_context.b4 == 0) {
    uVar1 = 0x81000059;
  }
  else if (app_context.b4 == 3) {
    uVar1 = 0x8100005a;
  }
  else {
    if (app_context.b4 != 2) {
      return 0;
    }
    uVar1 = 0x8100009e;
  }
  win_open(0x20c4434,uVar1,2);
  return 0;
}


/* ==================== 0x02009f08 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009f08 Name: kc02_power_handler
 */

/* POWER (0x24): ui_mode_request(6,0) + state 0x020D41D4/5 */

undefined4 kc02_power_handler(int param_1,int *param_2)

{
  if ((param_1 == 1) && (*param_2 == 0)) {
    ui_mode_request(6,&stack0x00000000);
    if (app_context.b68 == 0) {
      app_context.b69 = app_context.b68;
      app_context.b68 = 2;
    }
  }
  return 0;
}


/* ==================== 0x02009f6c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009f6c Name: ui_key_cycle_state
 */

/* Track2b second-opinion confirm (names_01b.tsv): The param_2 == 1 and *param_3 == 0 guard cycles
   iRam020ccd98 through 0, 1, and 2, with boardIoctrl command 0x20000011 on transitions to 0 and 2.
   [was FUN_ram_02009f6c] */

undefined4 ui_key_cycle_state(undefined4 param_1,int param_2,int *param_3)

{
  if ((param_2 == 1) && (*param_3 == 0)) {
    if (iRam020ccd98 == 0) {
      iRam020ccd98 = 1;
    }
    else if (iRam020ccd98 == 1) {
      iRam020ccd98 = 2;
    }
    else {
      iRam020ccd98 = *param_3;
      boardIoctrl((int)(sbyte)app_context.b10,0x20000011,&stack0x00000000);
    }
    if (iRam020ccd98 == 2) {
      boardIoctrl((int)(sbyte)app_context.b10,0x20000011,1,register0x00000401);
    }
    FUN_ram_02008f0c(param_1);
  }
  return 0;
}


/* ==================== 0x0200a030 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200a030 Name: FUN_ram_0200a030
 */

void FUN_ram_0200a030(undefined4 param_1)

{
  undefined4 uVar1;
  int iVar2;
  undefined4 uVar3;
  
  iVar2 = configGet(0x18,&stack0x00000000);
  if (iVar2 == -0x7effff64) {
    uVar3 = win_find_child_by_id(param_1,4);
    uVar1 = 0x207ba28;
  }
  else {
    iVar2 = configGet(0x18,register0x00000401);
    if (iVar2 == -0x7effff65) {
      uVar3 = win_find_child_by_id(param_1,4);
      uVar1 = 0x207ba36;
    }
    else {
      iVar2 = configGet(0x18,register0x00000401);
      if (iVar2 == -0x7effff66) {
        uVar3 = win_find_child_by_id(param_1,4);
        uVar1 = 0x207ba2b;
      }
      else {
        iVar2 = configGet(0x18,register0x00000401);
        if (iVar2 == -0x7effff67) {
          uVar3 = win_find_child_by_id(param_1,4);
          uVar1 = 0x207ba2e;
        }
        else {
          iVar2 = configGet(0x18,register0x00000401);
          if (iVar2 == -0x7effff68) {
            uVar3 = win_find_child_by_id(param_1,4);
            uVar1 = 0x207ba3e;
          }
          else {
            iVar2 = configGet(0x18);
            if (iVar2 == -0x7effff69) {
              uVar3 = win_find_child_by_id(param_1,4);
              uVar1 = 0x207ba31;
            }
            else {
              iVar2 = configGet(0x18);
              if (iVar2 == -0x7effff6a) {
                uVar3 = win_find_child_by_id(param_1,4);
                uVar1 = 0x207ba35;
              }
              else {
                iVar2 = configGet(0x18);
                if (iVar2 == -0x7effff6b) {
                  uVar3 = win_find_child_by_id(param_1,4);
                  uVar1 = 0x207ba39;
                }
                else {
                  iVar2 = configGet(0x18);
                  if (iVar2 == -0x7effff6c) {
                    uVar3 = win_find_child_by_id(param_1,4);
                    uVar1 = 0x207ba3d;
                  }
                  else {
                    iVar2 = configGet(0x18);
                    if (iVar2 == -0x7effff6d) {
                      uVar3 = win_find_child_by_id(param_1,4);
                      uVar1 = 0x207ba41;
                    }
                    else {
                      iVar2 = configGet(0x18);
                      if (iVar2 == -0x7effff6e) {
                        uVar3 = win_find_child_by_id(param_1,4);
                        uVar1 = 0x207ba45;
                      }
                      else {
                        iVar2 = configGet(0x18);
                        if (iVar2 == -0x7effff6f) {
                          uVar3 = win_find_child_by_id(param_1,4);
                          uVar1 = 0x207ba49;
                        }
                        else {
                          iVar2 = configGet(0x18);
                          if (iVar2 == -0x7effff70) {
                            uVar3 = win_find_child_by_id(param_1,4);
                            uVar1 = 0x207ba4d;
                          }
                          else {
                            uVar3 = win_find_child_by_id(param_1,4);
                            uVar1 = 0x207ba51;
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  winSetResid(uVar3,uVar1);
  return;
}


/* ==================== 0x0200a310 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200a310 Name: FUN_ram_0200a310
 */

undefined4 FUN_ram_0200a310(undefined4 param_1)

{
  FUN_ram_0200a030(&stack0x00000000);
  FUN_ram_02009bc0(param_1);
  FUN_ram_02008bec(param_1);
  FUN_ram_02009358(param_1,0);
  FUN_ram_02009ccc(param_1,0,0);
  FUN_ram_02009d5c(param_1);
  return 0;
}


/* ==================== 0x0200a37c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200a37c Name: FUN_ram_0200a37c
 */

undefined4 FUN_ram_0200a37c(undefined4 param_1)

{
  undefined4 uVar1;
  
  iRam020ccd98 = 0;
  func_0x020075e8(&stack0x00000000);
  FUN_ram_02009ccc(param_1,0,0);
  FUN_ram_0200a030(param_1);
  FUN_ram_02009bc0(param_1);
  FUN_ram_02008bec(param_1);
  FUN_ram_02008d98(param_1);
  FUN_ram_02009358(param_1,0);
  FUN_ram_02009d5c(param_1);
  FUN_ram_0200917c(param_1);
  UNK_ram_020c8780 = 1;
  ui_show_record_indicator(param_1);
  if (iRam020ccda0 == 0) {
    iRam020ccda0 = func_0x02007744(0x28,0x20ccd9c);
  }
  if (iRam020ccd98 == 0) {
    uVar1 = win_find_child_by_id(param_1,0x1a);
    winSetVisible(uVar1,0);
  }
  FUN_ram_02008f0c(param_1);
  if (app_context.b68 == 0) {
    app_context.b69 = app_context.b68;
    app_context.b68 = 1;
  }
  func_0x02077cf4(0x10,0x20,0xd);
  return 0;
}


/* ==================== 0x0200a4bc ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200a4bc Name: ui_task_print_image
 */

/* ui_task_print_image - UI task (menu-mode msgs 0x1A/0x23): encodes the frame via 0x020087E8
   (watermark-aware JPEG encoder) then kc02_print_image(0x0205B0FC). */

undefined4 ui_task_print_image(undefined4 param_1,int param_2,int *param_3)

{
  uint uVar1;
  undefined4 uVar2;
  sbyte sVar3;
  int iVar4;
  int iVar5;
  char *pcVar6;
  short sVar7;
  undefined *puStack_34;
  int iStack_30;
  
  if (param_2 != 1) {
    return 0;
  }
  if (*param_3 != 0) {
    return 0;
  }
  sVar7 = 1;
  if (app_context.b59 != 0) goto LAB_ram_0200a600;
  uVar1 = (uint)app_context.b5b;
  if ((uVar1 - 7 < 2) || ((int)(uVar1 - 1) < 0)) {
    sVar7 = 1;
    goto LAB_ram_0200a600;
  }
  if (uVar1 == 1) {
    sVar7 = 3;
    goto LAB_ram_0200a600;
  }
  if (uVar1 == 2) {
    sVar7 = 5;
    goto LAB_ram_0200a600;
  }
  if (uVar1 != 3) {
    if (uVar1 == 4) {
      uVar1 = 5;
    }
    else {
      if (uVar1 != 5) {
        if (uVar1 != 6) goto LAB_ram_0200a600;
        uVar1 = 5;
        sVar7 = 3;
        goto LAB_ram_0200a588;
      }
      uVar1 = 10;
    }
  }
  sVar7 = 1;
LAB_ram_0200a588:
  iVar4 = XOSTimeGet();
  XOSTimeDly(100);
  sys_irq_clear();
  FUN_ram_02009358(param_1,1,uVar1);
  do {
    sys_irq_clear();
    iVar5 = XOSTimeGet();
    if (999 < (uint)(iVar5 - iVar4)) {
      uVar1 = uVar1 - 1 & 0xffff;
      FUN_ram_02009358(param_1,1,uVar1);
      if (uVar1 == 0) break;
      iVar4 = XOSTimeGet();
    }
    mode_photo_tick(0);
  } while( true );
  FUN_ram_02009358(param_1,0,0);
LAB_ram_0200a600:
  do {
    while( true ) {
      sVar7 = sVar7 + -1;
      if (sVar7 == -1) {
        FUN_ram_0200917c(param_1);
        return 0;
      }
      mode_photo_tick(0);
      if (1 < app_context.b4 - 3) break;
      boardIoctrl((int)(sbyte)app_context.b11,0x2000000a,0);
      if (iRam020ccd98 == 1) {
        boardIoctrl((int)(sbyte)app_context.b10,0x20000011);
      }
      if (((app_context.b59 == 0) && (app_context.b5b - 7 < 2)) &&
         (iVar4 = FUN_ram_02022720(), iVar4 != 0)) {
        photo_effect_edge_detect
                  (*(undefined4 *)(iVar4 + 4),*(undefined4 *)(iVar4 + 8),
                   *(undefined2 *)(iVar4 + 0x20),*(undefined2 *)(iVar4 + 0x22),
                   -(app_context.b5b ^ 7) >> 0x1f);
        hal_lcdDealBuffer(iVar4);
      }
      ax32xx_csiEnable(0);
      dispLayerUninit(0);
      iVar4 = FUN_ram_02008630();
      audio_playback_nv(0x2c,1,0);
      while (iVar5 = audioPlaybackGetStatus(), iVar5 == 1) {
        XOSTimeDly(10);
      }
      ax32xx_csiEnable(1);
      dispLayerInit(0);
      boardIoctrl((int)(sbyte)app_context.b11,0x2000000a,1);
      if (iRam020ccd98 == 1) {
        boardIoctrl((int)(sbyte)app_context.b10,0x20000011,0);
      }
      if (iVar4 == 0) {
        sys_irq_clear();
        ax32xx_csiEnable(0);
        ui_state_apply(7);
        if (app_context.b5f == 0) {
          XOSTimeDly(1000);
        }
        else {
          iVar4 = work_service(app_context._60_4_);
          uVar2 = app_context._60_4_;
          if (0 < iVar4) {
            iVar4 = work_service(app_context._60_4_);
            pcVar6 = manangerGetFileFullPathName(uVar2,iVar4,&iStack_30);
            if ((pcVar6 != (char *)0x0) && (iVar4 = open(pcVar6,1), -1 < iVar4)) {
              close(iVar4);
              FUN_ram_02009c1c(param_1,0x8100007f,0x81000056);
              video_present(1);
              iVar4 = open(pcVar6,1);
              if (-1 < iVar4) {
                sVar3 = kc02_print_file(iVar4,app_context.b61,app_context.b60,app_context.b0);
                if (sVar3 == 8) {
                  uVar2 = 0x81000057;
LAB_ram_0200a85c:
                  FUN_ram_02009c1c(param_1,0x8100007f,uVar2);
                  video_present(1);
                  XOSTimeDly(2000);
                }
                else {
                  if (sVar3 == 9) {
                    uVar2 = 0x81000058;
                    goto LAB_ram_0200a85c;
                  }
                  if (sVar3 == 0xe) {
                    uVar2 = 0x810000bb;
                    goto LAB_ram_0200a85c;
                  }
                }
                close(iVar4);
                FUN_ram_02009ccc(param_1,0,0);
                video_present(1);
              }
            }
          }
        }
        sys_irq_clear();
        ax32xx_csiEnable(1);
        ui_state_apply(app_context.b7);
      }
    }
    if (app_context.b5f != 0) {
      if (iRam020ccd98 == 1) {
        boardIoctrl((int)(sbyte)app_context.b10,0x20000011);
      }
      boardIoctrl((int)(sbyte)app_context.b11,0x2000000a,0);
      iVar4 = photo_encode_frame(&puStack_34,&iStack_30);
      boardIoctrl((int)(sbyte)app_context.b11,0x2000000a,1);
      if (iRam020ccd98 == 1) {
        boardIoctrl((int)(sbyte)app_context.b10,0x20000011,0);
      }
      if (iVar4 == 0) {
        FUN_ram_02009c1c(param_1,0x8100007f,0x81000056);
        video_present(1);
        XOSTimeDly(1000);
        sVar3 = kc02_print_image(puStack_34,app_context.b61,app_context.b60,app_context.b0);
        if (sVar3 == 8) {
          uVar2 = 0x81000057;
LAB_ram_0200a9bc:
          FUN_ram_02009c1c(param_1,0x8100007f,uVar2);
          video_present(1);
          XOSTimeDly(2000);
        }
        else {
          if (sVar3 == 9) {
            uVar2 = 0x81000058;
            goto LAB_ram_0200a9bc;
          }
          if (sVar3 == 0xe) {
            uVar2 = 0x810000bb;
            goto LAB_ram_0200a9bc;
          }
        }
        hal_sysMemFree(puStack_34);
        FUN_ram_02009ccc(param_1,0,0);
        video_present(1);
      }
      goto LAB_ram_0200a600;
    }
    XMsgQPost((undefined *)app_context._116_4_,(undefined *)0x1);
  } while( true );
}


/* ==================== 0x0200aa8c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200aa8c Name: ui_record_indicator_timeout
 */

/* Track2 evidence-based naming (likely, names_02.tsv): When the record indicator state
   app_context.b6d==2 and 800 time units have elapsed since DAT_ram_020ccda4, clears b6d and
   refreshes indicator and label.  [was FUN_ram_0200aa8c] */

void ui_record_indicator_timeout(undefined4 param_1)

{
  int iVar1;
  
  if ((app_context.b6d == 2) && (iVar1 = XOSTimeGet(), 800 < (uint)(iVar1 - DAT_ram_020ccda4))) {
    app_context.b6d = 0;
    ui_show_record_indicator(param_1,register0x00000401);
    ui_show_number_label(param_1);
  }
  return;
}


/* ==================== 0x02000404 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02000404 Name: app_main_loop
 */

/* per-frame: sys_irq_clear, system_tick(1), ui_key_dispatch, video_present */

void app_main_loop(void)

{
  sys_irq_clear(&stack0x00000000);
  system_tick(1);
  ui_key_dispatch();
  video_present(0);
  return;
}


/* ==================== 0x02000438 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02000438 Name: ui_invoke_key_13
 */

/* Track2 evidence-based naming (likely, names_01.tsv): UI callback that fetches the UI context with
   ui_get_ctx and raises key/action 0x13 via ui_key_invoke.  [was FUN_ram_02000438] */

void ui_invoke_key_13(void)

{
  undefined4 uVar1;
  
  uVar1 = ui_get_ctx(&stack0x00000000);
  ui_key_invoke(0x13,uVar1,0);
  return;
}


/* ==================== 0x02008188 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008188 Name: mode_manager_loop
 */

/* close->open->service of the selected task (curTask at 0x020CCD58) */

void mode_manager_loop(void)

{
  uint uVar1;
  int iVar2;
  int iVar3;
  
  iVar3 = 0;
  while (DAT_ram_020ccd50 != 0xb) {
    if (puRam020ccd58 != (undefined *)0x0) {
      if (*(code **)(puRam020ccd58 + 0xc) != (code *)0x0) {
        (**(code **)(puRam020ccd58 + 0xc))(*(undefined4 *)(puRam020ccd58 + 4),register0x00000401);
      }
      winDestroyDeskTopChildWin();
    }
    iVar2 = iVar3 + 1;
    uiHeapInfor(iVar3);
    uVar1 = DAT_ram_020ccd50;
    DAT_ram_020ccd54 = DAT_ram_020ccd50;
    DAT_ram_020ccd50 = 0xc;
    puRam020ccd58 = task_array[uVar1];
    if (puRam020ccd58 == (undefined *)0x0) {
      DAT_ram_020ccd54 = 1;
      puRam020ccd58 = task_array[1];
    }
    if (*(code **)(puRam020ccd58 + 8) != (code *)0x0) {
      (**(code **)(puRam020ccd58 + 8))(*(undefined4 *)(puRam020ccd58 + 4));
    }
    if (DAT_ram_020ccd54 != 1) {
      video_present(1);
    }
    while (iVar3 = iVar2, 0xb < DAT_ram_020ccd50) {
      app_main_loop();
      if (*(code **)(puRam020ccd58 + 0x10) != (code *)0x0) {
        (**(code **)(puRam020ccd58 + 0x10))(*(undefined4 *)(puRam020ccd58 + 4));
      }
    }
  }
  return;
}


/* ==================== 0x02004588 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02004588 Name: dispLayerInit
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: int dispLayerInit(INT8U layer)   [ax32_platform_demo/inc/display.h] */

int dispLayerInit(uint param_1)

{
  int iVar1;
  short sVar2;
  int iVar3;
  undefined *p0;
  uint uVar4;
  ushort local_20;
  ushort local_1e;
  ushort local_1c;
  ushort local_1a;
  ushort local_18;
  ushort local_16;
  
  param_1 = param_1 & 0xff;
  local_20 = 0;
  local_1e = 0;
  local_1c = 0;
  local_1a = 0;
  local_18 = 0;
  local_16 = 0;
  if (2 < param_1) {
    return -1;
  }
  iVar1 = param_1 * 0x10;
  hal_lcdGetOSDShowingResolution(&local_20,&local_1e);
  hal_lcdGetOSDShowingPosition(&local_1c,&local_1a);
  display_get_mode(&local_18,&local_16);
  (&DAT_ram_020c8a08)[param_1 * 8] = local_20;
  (&DAT_ram_020c8a0a)[param_1 * 8] = local_1e;
  *(ushort *)(&DAT_ram_020c8a0c + iVar1) = local_1c;
  *(ushort *)(&DAT_ram_020c8a0e + iVar1) = local_1a;
  *(undefined2 *)(&DAT_ram_020c8a04 + iVar1) = 0;
  if (param_1 != 1) {
    if (param_1 != 0) {
      return 0;
    }
    dispLayerSetRatio(0,0xff);
    hal_lcdSetRoate(0xff);
    hal_csiResolutionGet(&local_20,&local_1e);
    hal_lcdPreviewStart(local_20,local_1e);
    _DAT_ram_020c8a04 = 1;
    _DAT_ram_020c8a06 = 0;
    _DAT_ram_020c8a10 = 0;
    return 0;
  }
  p0 = hal_sysMemMalloc(0x400,0x20);
  uVar4 = hal_lcdGetShowingRotate();
  FUN_ram_02006444(p0);
  if (uVar4 == 0) {
    iVar1 = (int)(short)local_1c;
    iVar3 = (int)(short)local_1a;
  }
  else {
    if (uVar4 == 4) {
      iVar1 = (int)(short)local_1a;
      sVar2 = (local_18 - local_1c) - local_20;
    }
    else {
      if (uVar4 == 5) {
        iVar3 = (int)(short)local_1c;
        iVar1 = (int)((((uint)local_16 - (uint)local_1a) - (uint)local_1e) * 0x10000) >> 0x10;
        goto LAB_ram_0200470c;
      }
      sVar2 = (local_16 - local_1a) - local_1e;
      uVar4 = uVar4 & 0xff;
      iVar1 = (int)((((uint)local_18 - (uint)local_1c) - (uint)local_20) * 0x10000) >> 0x10;
    }
    iVar3 = (int)sVar2;
  }
LAB_ram_0200470c:
  lcd_osd_config_layer(0,iVar1,iVar3,p0,uVar4);
  hal_osdEnable(0,1);
  hal_sysMemFree(p0);
  DAT_ram_020c8a20 = 0;
  DAT_ram_020c8a16 = 0;
  DAT_ram_020c8a14 = 1;
  return 0;
}


/* ==================== 0x020041b0 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020041b0 Name: dispLayerUninit
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: int dispLayerUninit(INT8U layer)   [ax32_platform_demo/inc/display.h] */

int dispLayerUninit(sbyte param_1)

{
  byte p0;
  
  if (param_1 == 1) {
    p0 = 0;
  }
  else {
    if (param_1 != 2) {
      if (param_1 != 0) {
        return 0;
      }
      hal_lcdPreviewStop();
      return 0;
    }
    p0 = 1;
  }
  hal_osdEnable(p0,0);
  return 0;
}


/* ==================== 0x02004944 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02004944 Name: ui_state_apply
 */

/* state==7 special; else table 0x0207B908[state] */

undefined4 ui_state_apply(uint param_1)

{
  param_1 = param_1 & 0xff;
  if (param_1 == 7) {
    hal_lcdSetPIPEnable(0);
  }
  else {
    if (5 < param_1) {
      return 0xffffffff;
    }
    (*(code *)ui_state_dispatch[param_1])(&stack0x00000000);
  }
  return 0;
}


/* ==================== 0x020049f8 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020049f8 Name: mjpeg_start_recording
 */

/* Track2 evidence-based naming (likely, names_01.tsv): Computes the CSI/LCD crop and calls
   hal_mjpegAVIStart for 720p/VGA AVI recording, guarded by the record-active flags
   DAT_ram_020c8a01/UNK_ram_020c8a00.  [was FUN_ram_020049f8] */

undefined4 mjpeg_start_recording(uint param_1,sbyte param_2,ushort param_3,ushort param_4)

{
  ushort unaff_r2;
  int iVar1;
  byte bVar2;
  uint uVar3;
  byte bVar4;
  undefined1 *puVar5;
  ushort unaff_r18;
  uint uVar6;
  uint uVar7;
  ushort local_1c;
  ushort local_1a;
  
  param_1 = param_1 & 0xff;
  hal_csiResolutionGet(&local_1c,&local_1a);
  uVar3 = (uint)local_1c;
  if (uVar3 == 0x500) {
    if (param_1 < 0x11) {
      iVar1 = (int)((uint)app_context.b8 * 5) / -3 + 100;
      uVar3 = (iVar1 * 0x2d0) / 100 & 0xfffe;
      uVar6 = (iVar1 * 0x500) / 100 + 0x1fU & 0xffe0;
      param_4 = (ushort)((int)(local_1a - uVar3) / 2);
      param_3 = (ushort)((int)(0x500 - uVar6) / 2);
      unaff_r2 = param_4 + (short)uVar3;
      unaff_r18 = param_3 + (short)uVar6;
    }
    hal_lcdSetCsiCrop(param_3,param_4,unaff_r18,unaff_r2);
    if (param_2 == 0) {
      puVar5 = &DAT_ram_020c8a01;
      if (DAT_ram_020c8a01 != 0) {
        return 0;
      }
      bVar2 = 0;
      bVar4 = 0xd0;
LAB_ram_02004c98:
      hal_mjpegAVIStart(local_1c,local_1a,bVar2,bVar4);
      *puVar5 = 1;
      return 0;
    }
    bVar2 = 0;
    bVar4 = 0xd0;
  }
  else {
    if (param_1 < 0x11) {
      uVar7 = (*(int *)(&DAT_ram_0207b8c4 + param_1 * 4) * uVar3) / 100 & 0xfffe;
      uVar6 = (*(int *)(&DAT_ram_0207b8c4 + param_1 * 4) * (uint)local_1a) / 100 & 0xfffe;
      param_3 = (ushort)((int)(uVar3 - uVar7) / 2);
      param_4 = (ushort)((int)(local_1a - uVar6) / 2);
      unaff_r18 = param_3 + (short)uVar7;
      unaff_r2 = param_4 + (short)uVar6;
    }
    hal_lcdSetCsiCrop(param_3,param_4,unaff_r18,unaff_r2);
    if (param_2 == 0) {
      puVar5 = &UNK_ram_020c8a00;
      if (UNK_ram_020c8a00 != 0) {
        return 0;
      }
      bVar2 = 0x80;
      bVar4 = 0x68;
      goto LAB_ram_02004c98;
    }
    bVar2 = 0x80;
    bVar4 = 0x68;
  }
  hal_mjpegAVIStart(local_1c,local_1a,bVar2,bVar4);
  return 0;
}


/* ==================== 0x02012304 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02012304 Name: win_open
 */

/* Track2 evidence-based naming (likely, names_03.tsv): window open helper: looks up/reuses a window
   handle, registers the msg function, sends message 0x22 and returns the handle; used with several
   window descriptors across the UI.  [was FUN_ram_02012304] */

undefined4 win_open(undefined4 *param_1,uint param_2)

{
  uint *puVar1;
  uint uVar2;
  uint *puVar3;
  int iVar4;
  undefined4 local_38 [3];
  uint *local_2c;
  uint local_28;
  uint local_24 [5];
  
  if (5 < param_2) {
    param_2 = 5;
  }
  puVar1 = &local_28;
  puVar3 = (uint *)register0x00000401;
  for (uVar2 = 0; puVar1 = puVar1 + 1, uVar2 != param_2; uVar2 = uVar2 + 1) {
    *puVar1 = *puVar3;
    puVar3 = puVar3 + 1;
  }
  local_28 = param_2;
  if (param_1[2] == 0) {
    if (param_1[1] != 0) {
      *(undefined4 **)(param_1[1] + 0x24) = param_1;
    }
    iVar4 = win_create_widget_list(&LAB_ram_02012150,0);
    if (iVar4 != 0) {
      msgFuncRegister((msgDealInfor *)*param_1);
      param_1[2] = iVar4;
      goto LAB_ram_020123b8;
    }
  }
  else if (*(sbyte *)(param_1 + 3) != 0) {
LAB_ram_020123b8:
    local_38[0] = 0x22;
    local_2c = &local_28;
    winSendMsg(param_1[2],local_38);
    return param_1[2];
  }
  return 0;
}


/* ==================== 0x02022720 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02022720 Name: FUN_ram_02022720
 */

undefined4 FUN_ram_02022720(void)

{
  return DAT_ram_020cd788;
}


/* ==================== 0x020250a4 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020250a4 Name: win_create_widget_list
 */

/* Track2b second-opinion confirm (names_03.tsv): Invokes the first descriptor's procedure to obtain
   a handle, then walks 0x30-byte descriptors and invokes their procedures, notifying each result
   with winSendMsgToParentNoData(...,0x1a). The handle flag at +0x1a and winSetbgColor further
   corroborate the window/widget context.  [was FUN_ram_020250a4] */

int win_create_widget_list(undefined4 *param_1,undefined4 param_2,undefined4 param_3)

{
  undefined4 uVar1;
  int local_14;
  
  if ((param_1 != (undefined4 *)0x0) &&
     (local_14 = (*(code *)*param_1)(param_3,param_2), local_14 != 0)) {
    if ((*(ushort *)(local_14 + 0x1a) & 2) == 0) {
      while( true ) {
        param_1 = param_1 + 0xc;
        if ((code *)*param_1 == (code *)0x0) break;
        uVar1 = (*(code *)*param_1)(param_1,param_2);
        winSendMsgToParentNoData(uVar1,0x1a);
      }
      return local_14;
    }
    winSetbgColor(&local_14);
  }
  return 0;
}


/* ==================== 0x02025154 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02025154 Name: win_find_child_by_id
 */

/* Track2 evidence-based naming (likely, names_07.tsv): Posts message 0x19 to the window and then
   walks the child chain at param_1+0x2c comparing widgetGetId(child) with the requested id (only
   searched when id > 0x13), returning the matching child pointer.  [was FUN_ram_02025154] */

int win_find_child_by_id(uint param_1,uint param_2)

{
  uint uVar1;
  undefined4 local_20 [2];
  int local_18;
  uint local_14;
  
  param_2 = param_2 & 0xffff;
  if (((int)(param_1 | -param_1) < 0) &&
     (local_18 = -((int)(((param_2 ^ 0xffffffff) & 0xffff) - 1) >> 0x1f), local_18 == 0)) {
    local_20[0] = 0x19;
    local_14 = param_2;
    winSendMsg(local_20);
    if (local_18 != 0) {
      return local_18;
    }
    if (0x13 < param_2) {
      do {
        uVar1 = widgetGetId(*(undefined4 *)(param_1 + 0x2c));
        if (uVar1 == param_2) {
          return *(int *)(param_1 + 0x2c);
        }
        param_1 = *(uint *)(param_1 + 0x2c);
      } while (param_1 != 0);
    }
  }
  return 0;
}


/* ==================== 0x020253c8 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020253c8 Name: frameWinCreateIndirect
 */

/* SDK prototype: winHandle frameWinCreateIndirect(widgetCreateInfor* infor,winHandle parent,winCB
   cb)   [sUI/frameWin.h] */

void frameWinCreateIndirect(int p0,undefined4 param_2,undefined4 param_3)

{
  int iVar1;
  
  iVar1 = frameWinCreate((int)*(short *)(p0 + 6),(int)*(short *)(p0 + 8),*(undefined2 *)(p0 + 10),
                         *(undefined2 *)(p0 + 0xc),param_2,*(undefined2 *)(p0 + 0xe),param_3,
                         &stack0x00000000);
  winSetfgColor(iVar1,*(undefined1 *)(p0 + 0x10));
  if (iVar1 != 0) {
    *(undefined4 *)(iVar1 + 0x84) = *(undefined4 *)(p0 + 0x24);
  }
  return;
}


/* ==================== 0x0202566c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202566c Name: imageIconCreateIndirect
 */

/* SDK prototype: winHandle imageIconCreateIndirect(widgetCreateInfor* infor,winHandle parent,winCB
   cb)   [sUI/imageIcon.h] */

int imageIconCreateIndirect(int p0,undefined4 param_2)

{
  int iVar1;
  
  iVar1 = imageIconCreate((int)*(short *)(p0 + 6),(int)*(short *)(p0 + 8),*(undefined2 *)(p0 + 10),
                          *(undefined2 *)(p0 + 0xc),param_2,*(ushort *)(p0 + 0xe) | 0x40,
                          *(undefined2 *)(p0 + 4),&stack0x00000000);
  if (iVar1 != 0) {
    *(undefined4 *)(iVar1 + 0x38) = *(undefined4 *)(p0 + 0x14);
    *(undefined1 *)(iVar1 + 0x3c) = *(undefined1 *)(p0 + 0x18);
    winSetfgColor(iVar1,*(undefined1 *)(p0 + 0x10));
  }
  return iVar1;
}


/* ==================== 0x0202808c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202808c Name: rectCreateIndirect
 */

/* SDK prototype: winHandle rectCreateIndirect(widgetCreateInfor* infor,winHandle parent,winCB cb)
   [sUI/rect.h] */

int rectCreateIndirect(int p0,undefined4 param_2)

{
  int iVar1;
  
  iVar1 = rectCreate((int)*(short *)(p0 + 6),(int)*(short *)(p0 + 8),*(undefined2 *)(p0 + 10),
                     *(undefined2 *)(p0 + 0xc),param_2,*(ushort *)(p0 + 0xe) | 0x40,
                     *(undefined2 *)(p0 + 4),&stack0x00000000);
  if (iVar1 != 0) {
    *(undefined1 *)(iVar1 + 0x38) = *(undefined1 *)(p0 + 0x21);
    winSetfgColor(iVar1,*(undefined1 *)(p0 + 0x10));
  }
  return iVar1;
}


/* ==================== 0x020283cc ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020283cc Name: FUN_ram_020283cc
 */

void FUN_ram_020283cc(int param_1,undefined4 param_2)

{
  int iVar1;
  
  iVar1 = func_0x02028310((int)*(short *)(param_1 + 6),(int)*(short *)(param_1 + 8),
                          *(undefined2 *)(param_1 + 10),*(undefined2 *)(param_1 + 0xc),param_2,
                          *(ushort *)(param_1 + 0xe) | 0x40,*(undefined2 *)(param_1 + 4),
                          &stack0x00000000);
  if (iVar1 != 0) {
    *(undefined4 *)(iVar1 + 0x38) = *(undefined4 *)(param_1 + 0x1c);
    *(undefined1 *)(iVar1 + 0x3d) = *(undefined1 *)(param_1 + 0x20);
    *(undefined1 *)(iVar1 + 0x3e) = *(undefined1 *)(param_1 + 0x21);
    *(undefined1 *)(iVar1 + 0x3c) = *(undefined1 *)(param_1 + 0x22);
    winSetfgColor(iVar1,*(undefined1 *)(param_1 + 0x10));
    *(undefined1 *)(iVar1 + 0x40) = *(undefined1 *)(param_1 + 0x29);
    *(undefined1 *)(iVar1 + 0x41) = *(undefined1 *)(param_1 + 0x28);
    *(undefined1 *)(iVar1 + 0x42) = *(undefined1 *)(param_1 + 0x2a);
    *(undefined1 *)(iVar1 + 0x43) = *(undefined1 *)(param_1 + 0x2b);
    *(undefined1 *)(iVar1 + 0x3f) = *(undefined1 *)(param_1 + 0x2d);
  }
  return;
}


/* ==================== 0x02039dc8 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02039dc8 Name: hal_lcdPreviewStop
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: s32 hal_lcdPreviewStop(void)   [hal/inc/hal_lcd.h] */

int hal_lcdPreviewStop(void)

{
  ax32xx_csiLCDDmaEnable(0);
  ax32xx_pipCounterReset(1);
  hal_dispframeFree(DAT_ram_020ce97c);
  DAT_ram_020ce97c = (undefined *)0x0;
  return 0;
}


/* ==================== 0x02039e44 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02039e44 Name: hal_lcdDealBuffer
 */

/* SDK align: src_hal_lcd.c.o/hal_lcdDealBuffer score=1.0 segment */

void hal_lcdDealBuffer(undefined *param_1)

{
  undefined *p2;
  
  if (param_1[0x2a] == 4) {
    p2 = hal_dispframeMalloc(1);
    if (p2 == (undefined *)0x0) {
      hal_dispframeFree(param_1);
    }
    else {
      hal_rotateAdd(0,param_1,p2);
    }
  }
  else {
    ax32xx_intEnable(4,0);
    if (DAT_ram_020ce974 != (undefined *)0x0) {
      hal_dispframeFree(DAT_ram_020ce974);
    }
    DAT_ram_020ce974 = param_1;
    if (*(sbyte *)(DAT_ram_020ce980 + 0x27) == 1) {
      ax32xx_deUpdate();
    }
    ax32xx_intEnable(4,1);
  }
  return;
}


/* ==================== 0x02039f9c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02039f9c Name: hal_lcdPreviewStart
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: s32 hal_lcdPreviewStart(u16 width,u16 height)   [hal/inc/hal_lcd.h] */

int hal_lcdPreviewStart(ushort p0,ushort p1)

{
  undefined *puVar1;
  int iVar2;
  int iVar3;
  
  if (DAT_ram_020ce97c == (undefined *)0x0) {
    DAT_ram_020ce97c = hal_lcdGetIdleBuffer();
  }
  puVar1 = DAT_ram_020ce97c;
  if (((int)(DAT_ram_020ce980 | -DAT_ram_020ce980) < 0) &&
     ((int)((uint)DAT_ram_020ce97c | -(int)DAT_ram_020ce97c) < 0)) {
    hal_dispframeVideoCfg
              (DAT_ram_020ce97c,*(ushort *)(DAT_ram_020ce980 + 0x38),
               *(ushort *)(DAT_ram_020ce980 + 0x3a),*(ushort *)(DAT_ram_020ce980 + 0x3c),
               *(ushort *)(DAT_ram_020ce980 + 0x3e));
  }
  if (DAT_ram_020ce980 != 0) {
    ax32xx_csiLCDScaler(p0,p1,*(ushort *)(*(int *)(DAT_ram_020ce968 + 0xc) + 10),
                        *(ushort *)(*(int *)(DAT_ram_020ce968 + 0xc) + 8),DAT_ram_020ce984,
                        DAT_ram_020ce988,DAT_ram_020ce986,DAT_ram_020ce98a,
                        *(ushort *)(puVar1 + 0x28),(uint *)(DAT_ram_020ce980 + 0xd0));
  }
  iVar2 = 0x100000;
  ax32xx_csiOutputSet(0x20000,0);
  ax32xx_csiOutputSet(0x200000,1);
  ax32xx_csiLCDFrameSet(*(uint *)(puVar1 + 4),*(uint *)(puVar1 + 8));
  do {
    iVar3 = ax32xx_csiScalerCheck();
    if (iVar3 != 0) break;
    iVar2 = iVar2 + -1;
  } while (iVar2 != 0);
  ax32xx_csiLCDDmaEnable(1);
  return 0;
}


/* ==================== 0x0203a84c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203a84c Name: lcd_commit_video_frame
 */

/* Track2 evidence-based naming (likely, names_08.tsv): Takes an idle LCD buffer
   (hal_lcdGetIdleBuffer, printing "buf full" when there is none), programs the video layer through
   hal_dispframeVideoCfg and ax32xx_csiLCDFrameSet using the crop fields of DAT_ram_020ce980 /
   DAT_ram_020ce968, then kicks the LCD DMA to show the captured frame.  [was FUN_ram_0203a84c] */

void lcd_commit_video_frame(void)

{
  undefined *p0;
  
  p0 = hal_lcdGetIdleBuffer();
  if (p0 == (undefined *)0x0) {
    uart_Printf(s_buf_full_ram_02081d84,register0x00000401);
  }
  else {
    hal_dispframeVideoCfg
              (p0,*(ushort *)(DAT_ram_020ce980 + 0x38),*(ushort *)(DAT_ram_020ce980 + 0x3a),
               *(ushort *)(DAT_ram_020ce980 + 0x3c),*(ushort *)(DAT_ram_020ce980 + 0x3e));
    ax32xx_csiLCDFrameSet
              (*(int *)(p0 + 4) +
               (uint)*(ushort *)(DAT_ram_020ce968 + 4) +
               (uint)*(ushort *)(DAT_ram_020ce968 + 6) * (uint)*(ushort *)(p0 + 0x28),
               *(int *)(p0 + 8) +
               (uint)*(ushort *)(DAT_ram_020ce968 + 4) +
               (uint)(*(ushort *)(DAT_ram_020ce968 + 6) >> 1) * (uint)*(ushort *)(p0 + 0x28));
    DAT_ram_020ce97c = p0;
    ax32xx_csiLCDDmaKick();
  }
  return;
}


/* ==================== 0x0203ad2c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203ad2c Name: FUN_ram_0203ad2c
 */

undefined4 FUN_ram_0203ad2c(byte param_1,byte param_2,byte param_3,byte param_4)

{
  sbyte sVar1;
  ushort p0;
  ushort p1;
  int unaff_r2;
  int unaff_r14;
  uint p0_00;
  uint uVar2;
  uint p3;
  uint p1_00;
  
  ax32xx_videoSetScale(param_1,param_2,param_3,param_4);
  p0 = *(ushort *)(*(int *)(unaff_r14 + 0x18) + 0x20);
  p1 = *(ushort *)(*(int *)(unaff_r14 + 0x18) + 0x22);
  ax32xx_osdSetUpscaler(p0,p1,p0,p1,(uint *)0x0);
  ax32xx_videoEnable(1);
  ax32xx_deUpdate();
  if (*(sbyte *)(unaff_r2 + 0x20) == 1) {
    func_0x02035a6c(*(undefined1 *)(unaff_r2 + 0x21),
                    -((*(char *)(unaff_r2 + 0x27) ^ 0xffffffff) & 0xff) >> 0x1f);
    ax32xx_lcdInit();
    ax32xx_lcdIRQEnable(0x13,1);
    func_0x02035720(1);
    ax32xx_lcdPreLineSet(7);
    ax32xx_lcdSignalSet(0,0,1,0,0,1,0,0);
    ax32xx_lcdBusWidth((uint)*(char *)(unaff_r2 + 0x21));
    ax32xx_lcdClkSet(0x38);
    ax32xx_lcdSyncSet(0,0,0,0);
    ax32xx_lcdDESignalSet(0,0,0,0);
    ax32xx_lcdPositionSet(0,0);
    ax32xx_lcdResolutionSet((uint)*(ushort *)(unaff_r2 + 0x34),(uint)*(ushort *)(unaff_r2 + 0x36));
    ax32xx_lcdWindowSizeSet(*(ushort *)(unaff_r2 + 0x34) - 1,*(ushort *)(unaff_r2 + 0x36) - 1);
    ax32xx_lcdDataModeSet
              (*(uint *)(unaff_r2 + 0x2c),*(uint *)(unaff_r2 + 0x30),*(byte *)(unaff_r2 + 0x5d),
               *(byte *)(unaff_r2 + 0x5e));
    ax32xx_lcdClkNumberSet
              ((uint)*(ushort *)(unaff_r2 + 0x34) * (uint)*(ushort *)(unaff_r2 + 0x36) *
               (uint)*(char *)(unaff_r2 + 0x5c) - 1);
    ax32xx_lcdEndLineSet(0,0);
    ax32xx_lcdEnable(1);
    func_0x020394b8(unaff_r2 + 0x22,*(undefined4 *)(unaff_r2 + 0x4c));
    ax32xx_lcdClkSet((uint)*(char *)(unaff_r2 + 0x28));
    ax32xx_sysCpuMsDelay(1);
    sVar1 = *(sbyte *)(unaff_r2 + 0x27);
    if (sVar1 == 0) {
      ax32xx_lcdTeMode(0,1);
      ax32xx_lcdIRQEnable(0x14,1);
    }
    else if (sVar1 == 1) {
      ax32xx_lcdISRRegister(0x14,0x203948c);
      ax32xx_lcdIRQEnable(0x14,1);
      ax32xx_lcdTeMode(0,1);
    }
    else if (sVar1 == -1) {
      ax32xx_lcdKick();
    }
  }
  else {
    func_0x02035b50(*(undefined1 *)(unaff_r2 + 0x21),
                    -((*(char *)(unaff_r2 + 0x66) ^ 0xffffffff) & 0xff) >> 0x1f);
    hal_lcdParaRGB(unaff_r2,*(undefined4 *)(unaff_r2 + 0x4c));
    ax32xx_lcdInit();
    ax32xx_lcdIRQEnable(0x11,1);
    func_0x02035720(0);
    ax32xx_lcdPreLineSet(7);
    ax32xx_lcdSignalSet(*(byte *)(unaff_r2 + 0x60),*(char *)(unaff_r2 + 0x5f) & '\x01',
                        *(char *)(unaff_r2 + 0x66) & '\x01',*(char *)(unaff_r2 + 0x67) & '\x01',
                        *(byte *)(unaff_r2 + 0x68),
                        (byte)(-((*(char *)(unaff_r2 + 0x66) ^ 0xffffffff) & 0xff) >> 0x1f),
                        (byte)(-((*(char *)(unaff_r2 + 0x67) ^ 0xffffffff) & 0xff) >> 0x1f),
                        (byte)(-((*(byte *)(unaff_r2 + 0x68) ^ 0xffffffff) & 0xff) >> 0x1f));
    ax32xx_lcdBusWidth((uint)*(char *)(unaff_r2 + 0x21));
    if (*(int *)(unaff_r2 + 0x4c) != 0) {
      ax32xx_lcdBusEnable(1,0);
    }
    ax32xx_lcdClkSet((uint)*(char *)(unaff_r2 + 0x28));
    ax32xx_lcdSyncSet(0,(uint)*(char *)(unaff_r2 + 99),0,(uint)*(char *)(unaff_r2 + 0x60));
    p0_00 = (uint)*(char *)(unaff_r2 + 99) +
            (uint)*(char *)(unaff_r2 + 100) * (uint)*(char *)(unaff_r2 + 0x5c);
    uVar2 = (uint)*(char *)(unaff_r2 + 0x60) + (uint)*(char *)(unaff_r2 + 0x61);
    p1_00 = p0_00 + (uint)*(ushort *)(unaff_r2 + 0x34) * (uint)*(char *)(unaff_r2 + 0x5c);
    p3 = (*(ushort *)(unaff_r2 + 0x36) - 1) + uVar2;
    ax32xx_lcdDESignalSet(p0_00,p1_00,uVar2,p3);
    ax32xx_lcdPositionSet(p0_00,uVar2);
    ax32xx_lcdResolutionSet((uint)*(ushort *)(unaff_r2 + 0x34),(uint)*(ushort *)(unaff_r2 + 0x36));
    ax32xx_lcdWindowSizeSet
              (p1_00 + (uint)*(char *)(unaff_r2 + 0x65) * (uint)*(char *)(unaff_r2 + 0x5c),
               (uint)*(char *)(unaff_r2 + 0x62) + (uint)*(char *)(unaff_r2 + 0x60) + p3);
    ax32xx_lcdDataModeSet
              (*(uint *)(unaff_r2 + 0x2c),*(uint *)(unaff_r2 + 0x30),*(byte *)(unaff_r2 + 0x5d),
               *(byte *)(unaff_r2 + 0x5e));
    ax32xx_lcdClkNumberSet(0);
    uVar2 = uVar2 + *(ushort *)(unaff_r2 + 0x36);
    ax32xx_lcdEndLineSet(uVar2,uVar2 + 2);
    ax32xx_lcdEnable(1);
  }
  hal_lcdFrameEndCallBack(0x2039354);
  hal_rotateInit();
  ax32xx_csiISRRegiser(1,0x203a2f0);
  return 0;
}


/* ==================== 0x02040384 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02040384 Name: hal_mjpegEncodeInit
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void hal_mjpegEncodeInit(void)   [hal/inc/hal_mjpeg.h] */

void hal_mjpegEncodeInit(void)

{
  char cVar1;
  uint uVar2;
  
  cVar1 = DAT_ram_020cf634;
  uVar2 = (uint)DAT_ram_020cf634;
  if (uVar2 == 0) {
    DAT_ram_020cf634 = '\x01';
    DAT_ram_020d41f8 = cVar1;
    _DAT_ram_020d4210 = uVar2;
    _DAT_ram_020d4214 = uVar2;
    _DAT_ram_020d4228 = uVar2;
    _DAT_ram_020d422c = uVar2;
    _DAT_ram_020d4238 = uVar2;
    hal_mjpegSrcCSI(&stack0x00000000);
    isp_init();
  }
  return;
}


/* ==================== 0x0204606c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0204606c Name: mjpeg_set_frame_mode
 */

/* Track2 evidence-based naming (likely, names_08.tsv): Forces the JPEG frame-mode field _11646_4_
   to 2 when called with 0 and to 3 when called with non-zero (no-op when already at the target
   value); mode 3 is the only one in which the two span-copy blits run.  [was FUN_ram_0204606c] */

void mjpeg_set_frame_mode(int param_1)

{
  if (param_1 == 0) {
    if (JPEG_ram_020d7582._11646_4_ == 3) {
      JPEG_ram_020d7582._11646_4_ = 2;
    }
  }
  else if (JPEG_ram_020d7582._11646_4_ == 2) {
    JPEG_ram_020d7582._11646_4_ = 3;
  }
  return;
}


/* ==================== 0x020460b4 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020460b4 Name: FUN_ram_020460b4
 */

void FUN_ram_020460b4(int param_1)

{
  undefined4 uVar1;
  int *piVar2;
  uint uVar3;
  
  uVar1 = JPEG_ram_020d7582._11706_4_;
  if (JPEG_ram_020d7582._11646_4_ == 3) {
    piVar2 = (int *)JPEG_ram_020d7582._11710_4_;
    for (uVar3 = 0; uVar3 < (uint)JPEG_ram_020d7582._11718_4_; uVar3 = uVar3 + 1) {
      ax32xx_sysInit((uint *)(param_1 + *piVar2),(uint *)(uVar1 + *piVar2));
      piVar2 = piVar2 + 2;
    }
  }
  return;
}


/* ==================== 0x0205609c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0205609c Name: record_config_set
 */

/* pipeline map: mjpeg/audio parameters */

undefined4 record_config_set(int param_1,int param_2)

{
  undefined1 uVar1;
  undefined1 uVar2;
  undefined1 uVar3;
  undefined1 uVar4;
  undefined1 uVar5;
  undefined1 uVar6;
  undefined1 uVar7;
  undefined1 uVar8;
  undefined1 uVar9;
  undefined4 uVar10;
  
  uVar10 = 0xffffffff;
  if (JPEG_ram_020df023._16733_1_ == 0) {
    uVar2 = JPEG_ram_020d7582._15288_1_;
    uVar3 = JPEG_ram_020d7582._15289_1_;
    uVar5 = JPEG_ram_020df023._16799_1_;
    uVar6 = JPEG_ram_020df023._16800_1_;
    uVar8 = JPEG_ram_020df023._16802_1_;
    uVar9 = JPEG_ram_020df023._16821_1_;
    if (param_1 == 2) {
      JPEG_ram_020d7582._15298_4_ = param_2 * 1000;
      uVar4 = JPEG_ram_020d7582._15290_1_;
      uVar7 = JPEG_ram_020df023._16801_1_;
      JPEG_ram_020df023._16809_4_ = JPEG_ram_020d7582._15298_4_;
    }
    else {
      uVar1 = (undefined1)param_2;
      uVar4 = uVar1;
      uVar7 = uVar1;
      if (((param_1 != 3) &&
          (uVar3 = uVar1, uVar4 = JPEG_ram_020d7582._15290_1_, uVar6 = uVar1,
          uVar7 = JPEG_ram_020df023._16801_1_, param_1 != 4)) &&
         (uVar2 = uVar1, uVar3 = JPEG_ram_020d7582._15289_1_, uVar5 = uVar1,
         uVar6 = JPEG_ram_020df023._16800_1_, param_1 != 5)) {
        if (param_1 == 6) {
          JPEG_ram_020df023._16815_2_ = SUB42(param_2,0);
          JPEG_ram_020df023._16813_2_ = SUB42((uint)param_2 >> 0x10,0);
          uVar2 = JPEG_ram_020d7582._15288_1_;
          uVar5 = JPEG_ram_020df023._16799_1_;
        }
        else {
          uVar2 = JPEG_ram_020d7582._15288_1_;
          uVar5 = JPEG_ram_020df023._16799_1_;
          uVar8 = uVar1;
          if ((param_1 != 7) && (uVar8 = JPEG_ram_020df023._16802_1_, uVar9 = uVar1, param_1 != 8))
          {
            return 0xffffffff;
          }
        }
      }
    }
    JPEG_ram_020df023._16821_1_ = uVar9;
    JPEG_ram_020df023._16802_1_ = uVar8;
    JPEG_ram_020df023._16801_1_ = uVar7;
    JPEG_ram_020df023._16800_1_ = uVar6;
    JPEG_ram_020df023._16799_1_ = uVar5;
    JPEG_ram_020d7582._15290_1_ = uVar4;
    JPEG_ram_020d7582._15289_1_ = uVar3;
    JPEG_ram_020d7582._15288_1_ = uVar2;
    uVar10 = 0;
  }
  return uVar10;
}


/* ==================== 0x020588c0 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020588c0 Name: isp_set_ccm_preset
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* Track2 evidence-based naming (likely, names_09.tsv): Loads one of several 9-coefficient matrices
   selected by preset 1-5 into the ISP and programs it through isp_CCM_cfg.  [was FUN_ram_020588c0]
    */

void isp_set_ccm_preset(sbyte param_1)

{
  ushort *puVar1;
  ushort uVar2;
  
  puVar1 = _DAT_ram_020d73f4;
  if (*(int *)(_DAT_ram_020d1e84 + 0x18) != 0x500) {
    return;
  }
  if (param_1 == 1) {
    *_DAT_ram_020d73f4 = 0x128;
    puVar1[1] = 0;
    puVar1[2] = 0;
    puVar1[3] = 0x55;
    puVar1[4] = 0x110;
    puVar1[5] = 0;
    puVar1[6] = 0x55;
    uVar2 = 0;
  }
  else {
    if (param_1 != 2) {
      if (param_1 == 3) {
        *_DAT_ram_020d73f4 = 0x110;
        puVar1[1] = 0;
        puVar1[2] = 0x55;
        puVar1[3] = 0;
        puVar1[4] = 0x110;
        puVar1[5] = 0x55;
        puVar1[6] = 0;
        puVar1[7] = 0;
        uVar2 = 0x128;
      }
      else if (param_1 == 4) {
        uVar2 = 0x55;
        *_DAT_ram_020d73f4 = 0x55;
        puVar1[1] = 0x55;
        puVar1[2] = 0x55;
        puVar1[3] = 0x55;
        puVar1[4] = 0x55;
        puVar1[5] = 0x55;
        puVar1[6] = 0x55;
        puVar1[7] = 0x55;
      }
      else if (param_1 == 5) {
        *_DAT_ram_020d73f4 = 0x80;
        puVar1[1] = 0x80;
        puVar1[2] = 0;
        puVar1[3] = 0x80;
        puVar1[4] = 0x80;
        puVar1[5] = 0;
        puVar1[6] = 0x80;
        puVar1[7] = 0;
        uVar2 = 0x80;
      }
      else {
        *_DAT_ram_020d73f4 = 0x100;
        puVar1[1] = 0;
        puVar1[2] = 0;
        puVar1[3] = 0;
        puVar1[4] = 0x100;
        puVar1[5] = 0;
        puVar1[6] = 0;
        puVar1[7] = 0;
        uVar2 = 0x100;
      }
      goto LAB_ram_02058a8c;
    }
    *_DAT_ram_020d73f4 = 0x110;
    puVar1[1] = 0x55;
    puVar1[2] = 0;
    puVar1[3] = 0;
    puVar1[4] = 0x128;
    puVar1[5] = 0;
    puVar1[6] = 0;
    uVar2 = 0x55;
  }
  puVar1[7] = uVar2;
  uVar2 = 0x110;
LAB_ram_02058a8c:
  puVar1[8] = uVar2;
  isp_CCM_cfg(_DAT_ram_020d73f4,_DAT_ram_020d73f4[9],_DAT_ram_020d73f4[10],_DAT_ram_020d73f4[0xb]);
  return;
}


/* ==================== 0x0205b8c4 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0205b8c4 Name: video_present
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* LCD present / buffer blit */

void video_present(sbyte param_1)

{
  uint p0;
  int iVar1;
  undefined *p0_00;
  int local_18;
  
  boardIoctrl((int)(sbyte)app_context.b13,0x20000013,&local_18);
  if (local_18 == 0) {
    p0 = FUN_ram_02004840(1);
    memset(p0,0xfa,(uint)_DAT_ram_020d2ef6 * (uint)_DAT_ram_020d2ef4);
    ax32xx_sysDcacheWback(p0,(uint)_DAT_ram_020d2ef6 * (uint)_DAT_ram_020d2ef4);
    FUN_ram_020048f8(1,p0);
    ax32xx_deUpdate();
  }
  else {
    iVar1 = ui_close_all_windows();
    if (iVar1 == 0) {
      if (((param_1 == 0) && (app_context.b66 == 0)) && (app_context.b68 == 0)) {
        return;
      }
      app_context.b66 = 0;
    }
    ui_invoke_key_13();
    if (DAT_ram_020d2ef8 != 0) {
      p0_00 = (undefined *)FUN_ram_02004840(1);
      ax32xx_sys_dma_memkick
                (p0_00,_DAT_ram_020d2efc,(uint)_DAT_ram_020d2ef6 * (uint)_DAT_ram_020d2ef4);
      if (app_context.b68 != 0) {
        app_context.b68 = 0;
        app_context.b69 = 0;
        app_context.b66 = 1;
      }
      FUN_ram_020048f8(1,p0_00);
    }
    ax32xx_deUpdate();
    if (app_context.b68 != 0) {
      XOSTimeDly(0x1e);
    }
  }
  return;
}


/* ==================== 0x02007880 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02007880 Name: msgFuncRegister
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void msgFuncRegister(msgDealInfor* infor)   [ax32_platform_demo/application.h] */

void msgFuncRegister(msgDealInfor *p0)

{
  int iVar1;
  undefined4 *puVar2;
  kc02_task_map_entry *pkVar3;
  
  iVar1 = 0;
  do {
    puVar2 = (undefined4 *)((int)&DAT_ram_020ccc8c + iVar1);
    iVar1 = iVar1 + 4;
    *puVar2 = 0;
  } while (iVar1 != 0xb8);
  pkVar3 = task_map_builtin;
  while (pkVar3 = pkVar3 + 1, pkVar3->msg < 0x2e) {
    if (pkVar3->handler == (undefined *)0x0) break;
    (&DAT_ram_020ccc8c)[pkVar3->msg] = pkVar3->handler;
  }
  if (p0 != (msgDealInfor *)0x0) {
    while (p0->msgId < 0x2e) {
      if (p0->func == (undefined *)0x0) {
        return;
      }
      (&DAT_ram_020ccc8c)[p0->msgId] = p0->func;
      p0 = p0 + 1;
    }
  }
  return;
}


/* ==================== 0x020079b4 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020079b4 Name: ui_key_dispatch
 */

/* UI key dispatcher (l.jalr r14 at 0x02007A98); r4=1, r5=&flags, ret 0x02007A9C */

void ui_key_dispatch(void)

{
  uint uVar1;
  bool bVar2;
  undefined *puVar3;
  undefined3 extraout_var;
  int iVar4;
  undefined4 uVar5;
  code *pcVar6;
  sbyte sStack_15;
  uint uStack_14;
  
  puVar3 = XMsgQPend((undefined *)app_context._116_4_,&sStack_15);
  if ((sStack_15 == 0) && (uVar1 = (uint)puVar3 & 0xffff, uVar1 < 0x2e)) {
    uStack_14 = (uint)puVar3 >> 0x10;
    if (((uVar1 - 0x1a < 0x14) && (uStack_14 == 0)) &&
       ((((bVar2 = menuIsOpen(), CONCAT31(extraout_var,bVar2) != 0 ||
          (iVar4 = ui_mode_get_current(), iVar4 != 4)) && (uVar1 != 0x26)) &&
        ((iVar4 = ui_mode_get_current(), iVar4 != 5 || (iVar4 = audio_core_state(), iVar4 != 1))))))
    {
      FUN_ram_0200ce14();
    }
    pcVar6 = (code *)(&DAT_ram_020ccc8c)[uVar1];
    if (pcVar6 != (code *)0x0) {
      uVar5 = ui_get_ctx();
      (*pcVar6)(uVar5,1,&uStack_14);
    }
    pcVar6 = *(code **)(&DAT_ram_020ccbd4 + uVar1 * 4);
    if (pcVar6 != (code *)0x0) {
      uVar5 = ui_get_ctx();
      (*pcVar6)(uVar5,1,&uStack_14);
    }
  }
  return;
}


/* ==================== 0x02012150 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02012150 Name: FUN_ram_02012150
 */

void FUN_ram_02012150(uint *param_1)

{
  uint uVar1;
  undefined4 uVar2;
  uint uVar3;
  int iVar4;
  undefined4 uVar5;
  undefined4 *puVar6;
  undefined4 uStack_20;
  undefined4 uStack_1c;
  undefined4 uStack_18;
  
  uVar3 = *param_1;
  puVar6 = &uStack_20;
  uVar1 = param_1[1];
  if (uVar3 == 0x15) {
    uVar3 = param_1[3];
    uVar2 = 3;
    uStack_20 = *(undefined4 *)(uVar3 + 0x14);
    uStack_1c = *(undefined4 *)(uVar3 + 0x18);
    uStack_18 = *(undefined4 *)(uVar3 + 0x1c);
    uVar5 = 0x12;
LAB_ram_020122b4:
    ui_key_invoke(uVar5,uVar1,uVar2,puVar6);
  }
  else {
    if (uVar3 < 0x16) {
      if (uVar3 == 2) {
        if (*(undefined4 **)(uVar1 + 0x84) == (undefined4 *)0x0) {
          return;
        }
        msgFuncRegister((msgDealInfor *)**(undefined4 **)(uVar1 + 0x84));
        uVar2 = 0;
        uVar5 = 0x10;
        puVar6 = (undefined4 *)0x0;
        goto LAB_ram_020122b4;
      }
      if (uVar3 != 4) {
        return;
      }
      uVar5 = 0;
      uVar2 = **(undefined4 **)(uVar1 + 0x84);
      uVar3 = param_1[3];
      puVar6 = (undefined4 *)0x0;
    }
    else {
      if (uVar3 != 0x22) {
        if (uVar3 != 0x23) {
          return;
        }
        puVar6 = *(undefined4 **)(uVar1 + 0x84);
        if (puVar6 == (undefined4 *)0x0) {
          return;
        }
        msgDealByMsgInfor(*puVar6,0xf,uVar1,0);
        puVar6[2] = 0;
        return;
      }
      puVar6 = *(undefined4 **)(uVar1 + 0x84);
      if (puVar6 == (undefined4 *)0x0) {
        return;
      }
      iVar4 = *(int *)(uVar1 + 0x24);
      if (((iVar4 != 0) && ((*(ushort *)(iVar4 + 0x1a) & 0x10) != 0)) &&
         (*(undefined4 **)(iVar4 + 0x84) != (undefined4 *)0x0)) {
        msgDealByMsgInfor(**(undefined4 **)(iVar4 + 0x84),0x11,0);
      }
      uVar2 = *puVar6;
      uVar5 = *(undefined4 *)param_1[3];
      uVar3 = 0xe;
      puVar6 = (undefined4 *)param_1[3] + 1;
    }
    msgDealByMsgInfor(uVar2,uVar3,uVar1,uVar5,puVar6);
  }
  return;
}


/* ==================== 0x02022700 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02022700 Name: FUN_ram_02022700
 */

void FUN_ram_02022700(undefined4 param_1)

{
  DAT_ram_020cd788 = param_1;
  return;
}


/* ==================== 0x0203a2f0 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203a2f0 Name: FUN_ram_0203a2f0
 */

void FUN_ram_0203a2f0(void)

{
  uint uVar1;
  char cVar2;
  sbyte *psVar3;
  int iVar4;
  sbyte *psVar5;
  undefined *puVar6;
  undefined *p0;
  int iVar7;
  int iVar8;
  bool bVar9;
  ushort uStack_2c;
  ushort uStack_2a;
  
  puVar6 = DAT_ram_020ce97c;
  if (DAT_ram_020ce97c == (undefined *)0x0) {
    return;
  }
  hal_lcdUpdataVCH();
  psVar5 = psRam020ce96c;
  iVar4 = DAT_ram_020ce968;
  psVar3 = DAT_ram_020ce964;
  iVar7 = (uint)*(ushort *)(DAT_ram_020ce968 + 4) +
          (uint)(*(ushort *)(DAT_ram_020ce968 + 6) >> 1) * (uint)*(ushort *)(puVar6 + 0x28);
  iVar8 = (uint)*(ushort *)(DAT_ram_020ce968 + 4) +
          (uint)*(ushort *)(DAT_ram_020ce968 + 6) * (uint)*(ushort *)(puVar6 + 0x28);
  if ((psRam020ce96c[1] != 0) && (*psRam020ce96c == 1)) {
    ax32xx_pipConfig(psRam020ce96c[2],*(ushort *)(psRam020ce96c + 4),*(ushort *)(psRam020ce96c + 6),
                     *(ushort *)(psRam020ce96c + 10),*(ushort *)(psRam020ce96c + 8),
                     SUB41(-(uint)DAT_ram_020ce960 >> 0x1f,0));
    ax32xx_videoSetScaleLine((*(short *)(psVar5 + 6) + *(short *)(psVar5 + 8)) - 1,1);
  }
  bVar9 = *(sbyte *)(iVar4 + 1) != 0;
  if (bVar9) {
    hal_csiResolutionGet(&uStack_2c,&uStack_2a);
    ax32xx_csiLCDScaler(uStack_2c,uStack_2a,*(ushort *)(iVar4 + 10),*(ushort *)(iVar4 + 8),
                        DAT_ram_020ce984,DAT_ram_020ce988,DAT_ram_020ce986,DAT_ram_020ce98a,
                        *(ushort *)(puVar6 + 0x28),(uint *)(DAT_ram_020ce980 + 0xd0));
  }
  cVar2 = puVar6[0x2c];
  uVar1 = 4;
  puVar6[0x2c] = (sbyte)(cVar2 | 4);
  if (*psVar3 == 1) {
    uVar1 = 0xc;
  }
  if (((int)((uVar1 ^ uVar1 & (cVar2 | 4)) - 1) < 0) && (!bVar9)) {
    if (app_context.b58 != 0) {
      if (app_context.b5c == 2) {
        app_context.b5c = 0;
        FUN_ram_02022700(puVar6);
      }
      else {
        iVar7 = FUN_ram_02022720();
        if (iVar7 == 0) {
          FUN_ram_02022700(puVar6);
          app_context.b5c = 0;
        }
      }
      DAT_ram_020ce97c = (undefined *)0x0;
      psVar3[1] = 0;
      *(undefined1 *)(iVar4 + 1) = 0;
      return;
    }
    p0 = hal_lcdGetIdleBuffer();
    if (p0 != (undefined *)0x0) {
      hal_lcdDealBuffer(puVar6);
      hal_dispframeVideoCfg
                (p0,*(ushort *)(DAT_ram_020ce980 + 0x38),*(ushort *)(DAT_ram_020ce980 + 0x3a),
                 *(ushort *)(DAT_ram_020ce980 + 0x3c),*(ushort *)(DAT_ram_020ce980 + 0x3e));
      ax32xx_csiLCDFrameSet(*(int *)(p0 + 4) + iVar8,*(int *)(p0 + 8) + iVar7);
      DAT_ram_020ce97c = p0;
      goto LAB_ram_0203a50c;
    }
  }
  ax32xx_csiLCDFrameSet(*(int *)(puVar6 + 4) + iVar8,*(int *)(puVar6 + 8) + iVar7);
  puVar6[0x2c] = 0;
LAB_ram_0203a50c:
  psVar3[1] = 0;
  *(undefined1 *)(iVar4 + 1) = 0;
  ax32xx_csiLCDDmaKick();
  return;
}


/* ==================== 0x0203a91c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203a91c Name: hal_lcdFrameEndCallBack
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: s32 hal_lcdFrameEndCallBack(void (*callback)(void))   [hal/inc/hal_lcd.h] */

int hal_lcdFrameEndCallBack(undefined4 param_1)

{
  undefined4 uVar1;
  
  if (*(sbyte *)(DAT_ram_020ce980 + 0x20) == 1) {
    uVar1 = 0x13;
  }
  else {
    uVar1 = 0x11;
  }
  ax32xx_lcdISRRegister(uVar1,param_1,&stack0x00000000);
  return 0;
}


/* ==================== 0x0202030c ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202030c Name: present_frame
 */

void present_frame(void)

{
  if (DAT_ram_020cd6ac != 0) {
    ax32xx_sysDcacheFlush
              (*(uint *)(DAT_ram_020cd6ac + 4),
               (int)((uint)*(ushort *)(DAT_ram_020cd6ac + 0x20) *
                     (uint)*(ushort *)(DAT_ram_020cd6ac + 0x22) * 3) >> 1);
    dispLayerShow(DAT_ram_020cd6ac,0,0,0xff,register0x00000401);
    DAT_ram_020cd6ac = 0;
  }
  return;
}


/* ==================== 0x0202a1d0 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202a1d0 Name: ui_close_all_windows
 */

/* Track2 evidence-based naming (likely, names_08.tsv): Walks the window sibling chain (next pointer
   at +0x28) from the root window DAT_ram_020ce0d4 calling winDestroy on each, then flags a
   widget-pool leak when freeBlks != maxBlks or minFreeBlks == 0 (printing the memPool error text
   and dumping pool stats) and returns 1 if it ran.  [was FUN_ram_0202a1d0] */

undefined4 ui_close_all_windows(void)

{
  undefined4 uVar1;
  undefined *p0;
  
  uVar1 = 0;
  p0 = DAT_ram_020ce0d4;
  if (DAT_ram_020ce0d0 != 0) {
    while (DAT_ram_020ce0d0 != 0) {
      if (*(int *)(p0 + 0x28) == 0) {
        winDestroy(p0);
        break;
      }
      winDestroy(p0);
      p0 = *(undefined **)(p0 + 0x28);
    }
    if ((DAT_ram_020ce0c0 != DAT_ram_020ce0c4) || (DAT_ram_020ce0c8 == 0)) {
      DAT_ram_020ce0dd = 1;
    }
    if (DAT_ram_020ce0dd != 0) {
      FUN_ram_02028ab8(s_mempool_err_ram_0208069c,register0x00000401);
      uart_Printf(s_memPool_error_information__ram_020806a8);
      ui_widget_pool_dump(&DAT_ram_020ce0b8);
    }
    uVar1 = 1;
    DAT_ram_020ce0d0 = 0;
  }
  return uVar1;
}


/* ==================== 0x0202a110 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202a110 Name: winUpdate
 */

/* SDK prototype: void winUpdate(winHandle hWin)   [sUI/winManage.h] */

void winUpdate(undefined2 *param_1)

{
  if (param_1 != (undefined2 *)0x0) {
    if ((param_1[0xd] & 1) == 0) {
      DAT_ram_020ce0d0 = DAT_ram_020ce0d0 + 1;
    }
    param_1[6] = *param_1;
    param_1[7] = param_1[1];
    param_1[8] = param_1[2];
    param_1[9] = param_1[3];
    param_1[0xd] = param_1[0xd] | 1;
  }
  return;
}


/* ==================== 0x02028b04 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02028b04 Name: winSendMsg
 */

/* SDK prototype: void winSendMsg(winHandle handle,winMsg* msg)   [sUI/winManage.h] */

void winSendMsg(int param_1,int param_2)

{
  code *pcVar1;
  
  if ((param_1 != 0) && (pcVar1 = *(code **)(param_1 + 0x20), pcVar1 != (code *)0x0)) {
    *(int *)(param_2 + 4) = param_1;
    (*pcVar1)(param_2,&stack0x00000000);
  }
  return;
}


/* ==================== 0x020255c0 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020255c0 Name: imageIconCreate
 */

/* SDK prototype: winHandle imageIconCreate(int16 x0,int16 y0,uint16 width,uint16 height,winHandle
   parent,uint16 style,uint16 id)   [sUI/imageIcon.h] */

int imageIconCreate(short param_1,uint param_2,undefined2 param_3)

{
  undefined2 uVar1;
  uint uVar2;
  int iVar3;
  undefined4 uVar4;
  
  uVar1 = param_3;
  uVar2 = param_2 & 0xffff | 2;
  uVar4 = 0x40;
  iVar3 = winTouch((uiRect *)(int)param_1);
  widgetSetId(iVar3,uVar1,uVar4,uVar2,register0x00000401);
  if (iVar3 != 0) {
    *(undefined1 *)(iVar3 + 0x3d) = 1;
    *(undefined4 *)(iVar3 + 0x38) = 0x7fffffff;
    *(undefined1 *)(iVar3 + 0x3c) = 2;
    winSetfgColor(iVar3,0xff);
  }
  return iVar3;
}


/* ==================== 0x02028310 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02028310 Name: FUN_ram_02028310
 */

void FUN_ram_02028310(short param_1,uint param_2,undefined2 param_3)

{
  undefined2 uVar1;
  uint uVar2;
  int iVar3;
  undefined4 uVar4;
  
  uVar1 = param_3;
  uVar2 = param_2 & 0xffff | 2;
  uVar4 = 0x48;
  iVar3 = winTouch((uiRect *)(int)param_1);
  widgetSetId(iVar3,uVar1,uVar4,uVar2,register0x00000401);
  if (iVar3 != 0) {
    *(undefined1 *)(iVar3 + 0x3e) = 0xfb;
    *(undefined4 *)(iVar3 + 0x38) = 0x7fffffff;
    *(undefined1 *)(iVar3 + 0x3f) = 0xff;
    *(undefined1 *)(iVar3 + 0x43) = 0xff;
    *(undefined1 *)(iVar3 + 0x44) = 0xff;
    winSetfgColor(iVar3,0xff);
    *(undefined1 *)(iVar3 + 0x45) = 0;
  }
  return;
}


/* ==================== 0x02028d78 ==================== */
/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02028d78 Name: winDestroy
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void winDestroy(winHandle* hWin)   [sUI/winManage.h] */

void winDestroy(undefined *p0)

{
  uiRect *puVar1;
  uiRect *p1;
  uiRect *in_r4;
  bool bVar2;
  undefined3 extraout_var;
  undefined3 extraout_var_00;
  uiRect local_34;
  undefined4 local_28 [3];
  uiRect *local_1c;
  
  local_28[0] = 6;
  do {
    if ((-1 < (int)((uint)p0 | -(int)p0)) || (-1 < (int)(-DAT_ram_020ce0d0 | DAT_ram_020ce0d0))) {
      return;
    }
    if ((*(ushort *)(p0 + 0x1a) & 1) != 0) {
      DAT_ram_020ce0d0 = DAT_ram_020ce0d0 - 1;
      if ((*(ushort *)(p0 + 0x1a) & 4) != 0) {
        p1 = (uiRect *)(p0 + 0xc);
        if (in_r4 == (uiRect *)0x0) {
          do {
            local_1c = p1;
            winSendMsg(p0,local_28);
            p1 = *(uiRect **)(p1 + 1);
          } while (p1 != (uiRect *)0x0);
        }
        else {
          do {
            bVar2 = winContain(in_r4,p1);
            if (CONCAT31(extraout_var,bVar2) == 0) {
              puVar1 = p1;
              if (((*(ushort *)(p0 + 0x1a) & 2) == 0) &&
                 (bVar2 = winOverlapping(p1,in_r4), CONCAT31(extraout_var_00,bVar2) != 0)) {
                local_1c = &local_34;
                if (p1->x0 < in_r4->x0) {
                  local_34.x1 = in_r4->x0 + -1;
                  local_34.y0 = p1->y0;
                  local_34.y1 = p1->y1;
                  local_34.x0 = p1->x0;
                  local_1c = &local_34;
                  winSendMsg(p0,local_28);
                }
                if (p1->y0 < in_r4->y0) {
                  local_34.x0 = p1->x0;
                  if (p1->x0 < in_r4->x0) {
                    local_34.x0 = in_r4->x0;
                  }
                  local_34.x1 = p1->x1;
                  local_34.y1 = in_r4->y0 + -1;
                  local_34.y0 = p1->y0;
                  winSendMsg(p0,local_28);
                }
                if (in_r4->x1 < p1->x1) {
                  local_34.x0 = in_r4->x1 + 1;
                  local_34.y0 = p1->y0;
                  if (p1->y0 < in_r4->y0) {
                    local_34.y0 = in_r4->y0;
                  }
                  local_34.y1 = p1->y1;
                  local_34.x1 = p1->x1;
                  winSendMsg(p0,local_28);
                }
                if (p1->y1 <= in_r4->y1) goto LAB_ram_02028fc4;
                local_34.x0 = p1->x0;
                if (p1->x0 < in_r4->x0) {
                  local_34.x0 = in_r4->x0;
                }
                local_34.x1 = in_r4->x1;
                if (p1->x1 < in_r4->x1) {
                  local_34.x1 = p1->x1;
                }
                local_34.y0 = in_r4->y1 + 1;
                local_34.y1 = p1->y1;
                puVar1 = local_1c;
              }
              local_1c = puVar1;
              winSendMsg(p0,local_28);
            }
LAB_ram_02028fc4:
            p1 = *(uiRect **)(p1 + 1);
          } while (p1 != (uiRect *)0x0);
        }
      }
      winCreate(p0);
      *(ushort *)(p0 + 0x1a) = *(ushort *)(p0 + 0x1a) & 0xfffe;
    }
    p0 = *(undefined **)(p0 + 0x2c);
  } while( true );
}

