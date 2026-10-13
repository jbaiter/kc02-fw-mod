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

