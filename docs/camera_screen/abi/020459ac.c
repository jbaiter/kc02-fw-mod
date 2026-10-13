/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020459ac Name: FUN_ram_020459ac
 */

undefined4 FUN_ram_020459ac(int param_1)

{
  uint uVar1;
  uint unaff_r2;
  bool bVar2;
  int iVar3;
  uint uVar4;
  undefined *p1;
  int p2;
  undefined3 extraout_var;
  uint unaff_r14;
  ushort uStack_70;
  ushort uStack_6e;
  int aiStack_6c [17];
  
  if (JPEG_ram_020d7582._11646_4_ != 0) {
    uVar1 = Spr00000018;
    uVar4 = Spr00000018;
    Spr00000018 = uVar4 & 0xfffffffd;
    do {
      do {
        iVar3 = hal_BackRecDecodeStatusCheck();
      } while (iVar3 != 0);
      iVar3 = ax32xx_csiScalerCheck();
    } while (iVar3 == 0);
    iVar3 = nv_open(param_1);
    if (-1 < iVar3) {
      uVar4 = nv_size(param_1);
      p1 = hal_sysMemMalloc(uVar4,0x40);
      if (p1 != (undefined *)0x0) {
        p2 = nv_size(param_1);
        nv_read(iVar3,p1,p2);
        bVar2 = api_backrec_status();
        if (CONCAT31(extraout_var,bVar2) != 0) {
          backrec_sensor(false);
        }
        hal_lcdSetPIPEnable(0);
        mjpeg_parse_image_header(p1);
        hal_mjpegDecodeGetResolution(&uStack_70,&uStack_6e);
        JPEG_ram_020d7582._11646_4_ = 1;
        memcpy(aiStack_6c,0x2081e4c,0x44);
        if (app_context.b8 < 0x11) {
          unaff_r14 = (uint)(aiStack_6c[app_context.b8] * 0x2d0) / 100 & 0xfffe;
          unaff_r2 = (uint)(aiStack_6c[app_context.b8] * 0x500) / 100 + 0x1f & 0xffe0;
        }
        JPEG_ram_020d7582._11650_2_ = SUB42(unaff_r2,0);
        JPEG_ram_020d7582._11654_4_ = unaff_r2 * unaff_r14;
        JPEG_ram_020d7582._11652_2_ = SUB42(unaff_r14,0);
        iVar3 = func_0x02045250();
        if (-1 < iVar3) {
          hal_sysMemFree(p1);
          uVar4 = Spr00000018;
          Spr00000018 = uVar1 & 2 | uVar4;
          JPEG_ram_020d7582._11646_4_ = 3;
          return 0;
        }
        hal_sysMemFree(p1);
      }
    }
    uVar4 = Spr00000018;
    Spr00000018 = uVar1 & 2 | uVar4;
  }
  return 0xffffffff;
}

