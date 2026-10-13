/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020454fc Name: FUN_ram_020454fc
 */

/* WARNING: Restarted to delay deadcode elimination for space: ram */

undefined4 FUN_ram_020454fc(int param_1)

{
  ushort p3;
  uint uVar1;
  bool bVar2;
  int iVar3;
  uint uVar4;
  byte *p0;
  int iVar5;
  undefined3 extraout_var;
  byte *p0_00;
  byte *p0_01;
  uint unaff_r14;
  uint unaff_r18;
  uint uVar6;
  ushort uStack_74;
  ushort uStack_72;
  int aiStack_70 [17];
  
  if (JPEG_ram_020d7582._11646_4_ == 0) {
    return 0xffffffff;
  }
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
  if (iVar3 < 0) goto LAB_ram_02045960;
  uVar4 = nv_size(param_1);
  p0 = hal_sysMemMalloc(uVar4,0x40);
  if (p0 == (byte *)0x0) goto LAB_ram_02045960;
  iVar5 = nv_size(param_1);
  nv_read(iVar3,p0,iVar5);
  bVar2 = api_backrec_status();
  if (CONCAT31(extraout_var,bVar2) != 0) {
    backrec_sensor(false);
  }
  hal_lcdSetPIPEnable(0);
  mjpeg_parse_image_header(p0);
  hal_mjpegDecodeGetResolution(&uStack_74,&uStack_72);
  p3 = uStack_74;
  uVar6 = (uint)uStack_72;
  JPEG_ram_020d7582._11646_4_ = 1;
  uVar4 = (uint)(ushort)JPEG_ram_020d7582._11700_2_;
  if (uVar6 < uVar4) {
    p0_00 = hal_sysMemMalloc(0x19000,0x40);
    uVar4 = uVar6;
    if (p0_00 != (byte *)0x0) goto LAB_ram_02045650;
    p0_01 = (byte *)0x0;
  }
  else {
    p0_00 = (byte *)0x0;
    p3 = JPEG_ram_020d7582._11698_2_;
LAB_ram_02045650:
    iVar3 = p3 * uVar4;
    memcpy(aiStack_70,0x2081e4c,0x44);
    if (app_context.b8 < 0x11) {
      unaff_r14 = (uint)(aiStack_70[app_context.b8] * 0x2d0) / 100 & 0xfffe;
      unaff_r18 = (uint)(aiStack_70[app_context.b8] * 0x500) / 100 + 0x1f & 0xffe0;
    }
    uVar6 = (uint)uStack_72;
    JPEG_ram_020d7582._11650_2_ = SUB42(unaff_r18,0);
    JPEG_ram_020d7582._11652_2_ = SUB42(unaff_r14,0);
    if (uVar6 < unaff_r14) {
      unaff_r18 = (uint)uStack_74;
      p0_01 = hal_sysMemMalloc(0x19000,0x40);
      unaff_r14 = uVar6;
      if (p0_01 == (byte *)0x0) goto LAB_ram_02045938;
    }
    else {
      p0_01 = (byte *)0x0;
    }
    JPEG_ram_020d7582._11654_4_ = unaff_r18 * unaff_r14;
    iVar5 = hal_mjpegDecodePicture_2
                      (p0,(byte *)JPEG_ram_020d7582._11706_4_,
                       (byte *)(iVar3 + JPEG_ram_020d7582._11706_4_),p3,(ushort)uVar4);
    if ((-1 < iVar5) && (iVar5 = mjpeg_decode_wait_done(), iVar5 != 0)) {
      if (((int)((uint)p0_01 | -(int)p0_01) < 0) &&
         ((uint)uStack_72 < (uint)(ushort)JPEG_ram_020d7582._11652_2_)) {
        ax32xx_mjpB_Ctl_init
                  (0,4,unaff_r18,unaff_r14,(uint)(ushort)JPEG_ram_020d7582._11650_2_,
                   (uint)(ushort)JPEG_ram_020d7582._11652_2_);
        func_0x02037d00(unaff_r18 * unaff_r14 + JPEG_ram_020d7582._11658_4_);
        ax32xx_mjpB_dma_cfg((uint)p0_01,(uint)(p0_01 + 0x100000));
        hal_watermarkEnable(0,0);
        func_0x02037fc4();
        iVar5 = func_0x02037e50();
        if (((iVar5 < 0) ||
            (iVar5 = hal_mjpegDecodePicture_2
                               (p0_01,(byte *)JPEG_ram_020d7582._11658_4_,
                                (byte *)(JPEG_ram_020d7582._11658_4_ + JPEG_ram_020d7582._11654_4_),
                                JPEG_ram_020d7582._11650_2_,JPEG_ram_020d7582._11652_2_), iVar5 < 0)
            ) || (iVar5 = mjpeg_decode_wait_done(), iVar5 == 0)) goto LAB_ram_02045938;
      }
      if (((int)((uint)p0_00 | -(int)p0_00) < 0) &&
         ((uint)uStack_72 < (uint)(ushort)JPEG_ram_020d7582._11700_2_)) {
        ax32xx_mjpB_Ctl_init
                  (0,4,(uint)p3,uVar4,(uint)(ushort)JPEG_ram_020d7582._11698_2_,
                   (uint)(ushort)JPEG_ram_020d7582._11700_2_);
        func_0x02037d00(iVar3 + JPEG_ram_020d7582._11706_4_);
        ax32xx_mjpB_dma_cfg((uint)p0_00,(uint)(p0_00 + 0x19000));
        hal_watermarkEnable(0,0);
        func_0x02037fc4();
        iVar3 = func_0x02037e50();
        if ((iVar3 < 0) ||
           ((iVar3 = hal_mjpegDecodePicture_2
                               (p0_00,(byte *)JPEG_ram_020d7582._11706_4_,
                                (byte *)(JPEG_ram_020d7582._11706_4_ + JPEG_ram_020d7582._11702_4_),
                                JPEG_ram_020d7582._11698_2_,JPEG_ram_020d7582._11700_2_), iVar3 < 0
            || (iVar3 = mjpeg_decode_wait_done(), iVar3 == 0)))) goto LAB_ram_02045938;
      }
      iVar3 = func_0x02045250();
      if (-1 < iVar3) {
        hal_sysMemFree(p0);
        if ((int)((uint)p0_00 | -(int)p0_00) < 0) {
          hal_sysMemFree(p0_00);
        }
        if ((int)((uint)p0_01 | -(int)p0_01) < 0) {
          hal_sysMemFree(p0_01);
        }
        uVar4 = Spr00000018;
        Spr00000018 = uVar1 & 2 | uVar4;
        JPEG_ram_020d7582._11646_4_ = 3;
        return 0;
      }
    }
  }
LAB_ram_02045938:
  hal_sysMemFree(p0);
  if (p0_00 != (byte *)0x0) {
    hal_sysMemFree(p0_00);
  }
  if (p0_01 != (byte *)0x0) {
    hal_sysMemFree(p0_01);
  }
LAB_ram_02045960:
  uVar4 = Spr00000018;
  Spr00000018 = uVar1 & 2 | uVar4;
  return 0xffffffff;
}

