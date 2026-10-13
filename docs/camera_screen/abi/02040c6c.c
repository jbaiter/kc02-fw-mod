/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02040c6c Name: FUN_ram_02040c6c
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined4
FUN_ram_02040c6c(ushort param_1,ushort param_2,undefined1 param_3,undefined1 param_4,
                undefined1 param_5,sbyte param_6)

{
  ushort p4;
  ushort p5;
  undefined1 *puVar1;
  bool bVar2;
  byte p0;
  undefined3 extraout_var;
  uint p0_00;
  uint p1;
  int iVar3;
  uint uVar4;
  uint p2;
  ushort uStack_30;
  ushort uStack_2e;
  
  p2 = (uint)param_1;
  uVar4 = (uint)param_2;
  hal_mjpegEncodeInit();
  app_context.b6e = 1;
  DAT_ram_020d41fa = param_5;
  _DAT_ram_020d4208 = param_1;
  _DAT_ram_020d420a = param_2;
  if ((p2 < _DAT_ram_020d4204) || (uVar4 < _DAT_ram_020d4206)) {
    _DAT_ram_020d4230 = 1;
    DAT_ram_020d41f8 = 1;
    DAT_ram_020d41fb = mjpeg_quality_ctl_init(param_3);
    bVar2 = ax32xx_hvScaler((uint)_DAT_ram_020d4204,(uint)_DAT_ram_020d4206,p2,uVar4);
    if (CONCAT31(extraout_var,bVar2) == 0) {
      return 0xffffffff;
    }
    ax32xx_csihv_tomjp_dma_cfg(_DAT_ram_020d4210,_DAT_ram_020d4214,param_2,param_1);
    hal_streamInit(&DAT_ram_020d4254,&DAT_ram_020d4278,0x100,_DAT_ram_020d4238,_DAT_ram_020d423c);
    _DAT_ram_020d4248 = cache_pre_malloc(&DAT_ram_020d4254,_DAT_ram_020d423c);
    if (_DAT_ram_020d4248 == 0) {
      return 0xffffffff;
    }
    _DAT_ram_020d4250 = _DAT_ram_020d423c;
    ax32xx_mjpegEncodeBufferSet(_DAT_ram_020d4248,_DAT_ram_020d4248 + _DAT_ram_020d423c);
    ax32xx_mjpegEncodeInit(true,false);
    ax32xx_mjpeg_inlinebuf_init(_DAT_ram_020d4210,_DAT_ram_020d4214);
    ax32xx_mjpegEncodeSizeSet
              ((uint)_DAT_ram_020d4204,(uint)_DAT_ram_020d4206,(uint)_DAT_ram_020d4208,
               (uint)_DAT_ram_020d420a);
    ax32xx_mjpegEncodeQuilitySet(DAT_ram_020d41fb);
    ax32xx_mjpegEncodeInfoSet(0);
    video_watermark_update(p2,uVar4,param_4);
    ax32xx_csiISRRegiser(0);
    ax32xx_csiISRRegiser(3,0);
    puVar1 = (undefined1 *)0x0;
    if (param_6 == 0) {
      puVar1 = &LAB_ram_0204062c;
    }
    ax32xx_csiISRRegiser(9,puVar1);
    ax32xx_mjpegEA_ncodeISRRegister(&LAB_ram_0203fa08);
    uVar4 = 0x100000;
    if (param_6 != 0) {
      ax32xx_mjpeg_manual_on();
      ax32xx_intEnable(0x10,1);
      DAT_ram_020d41f9 = 0;
      _DAT_ram_020d4228 = 1;
      _DAT_ram_020d422c = 1;
      return 0;
    }
  }
  else {
    _DAT_ram_020d4230 = 2;
    DAT_ram_020d41f8 = 1;
    DAT_ram_020d4240 = 1;
    p0 = mjpeg_quality_ctl_init(param_3);
    ax32xx_mjpegEncodeInit(true,false);
    if (app_context.b8 < 0x11) {
      iVar3 = *(int *)((uint)app_context.b8 * 4 + 0x2081de8);
      hal_csiResolutionGet(&uStack_30,&uStack_2e);
      p1 = (iVar3 * (uint)uStack_2e) / 100 & 0xfffe;
      p0_00 = (uint)(iVar3 * 0x500) / 100 + 0x1f & 0xffe0;
      p4 = (ushort)((int)(uStack_30 - p0_00) / 2);
      p5 = (ushort)((int)(uStack_2e - p1) / 2);
      _DAT_ram_020d4214 = _DAT_ram_020d4210 + p0_00 * p1;
      ax32xx_mjpeg_inlinebuf_init(_DAT_ram_020d4210,_DAT_ram_020d4214);
      ax32xx_csiScaler(uStack_30,uStack_2e,(ushort)p0_00,(ushort)p1,p4,p5,(ushort)p0_00 + p4,
                       (ushort)p1 + p5);
      ax32xx_mjpegEncodeSizeSet(p0_00,p1,p2,uVar4);
      ax32xx_csiMJPEGFrameSet
                ((uint)_DAT_ram_020d4210,(uint)_DAT_ram_020d4214,JPEG_ram_020d7582._10782_4_,p0_00);
    }
    ax32xx_mjpegEncodeQuilitySet(p0);
    ax32xx_mjpegEncodeInfoSet(0);
    video_watermark_update(p2,uVar4,param_4);
    hal_streamInit(&DAT_ram_020d4254,&DAT_ram_020d4278,0x100,_DAT_ram_020d4238,_DAT_ram_020d423c);
    _DAT_ram_020d4248 = cache_pre_malloc(&DAT_ram_020d4254,_DAT_ram_020d423c);
    if (_DAT_ram_020d4248 == 0) {
      return 0xffffffff;
    }
    _DAT_ram_020d4250 = _DAT_ram_020d423c;
    if (param_6 == 0) {
      _DAT_ram_020d4250 = 0x32000;
    }
    ax32xx_mjpegEncodeBufferSet(_DAT_ram_020d4248,_DAT_ram_020d4248 + _DAT_ram_020d4250);
    ax32xx_csiISRRegiser(0,&LAB_ram_0204062c);
    ax32xx_csiISRRegiser(3,0);
    ax32xx_csiISRRegiser(9,0);
    if (param_6 == 0) {
      puVar1 = (undefined1 *)0x203f680;
    }
    else {
      puVar1 = &LAB_ram_0203fa08;
    }
    ax32xx_mjpegEA_ncodeISRRegister(puVar1);
    uVar4 = 0x80000;
  }
  ax32xx_csiOutputSet(uVar4,1);
  ax32xx_csiEnable(1);
  DAT_ram_020d41f9 = 0;
  _DAT_ram_020d4228 = 1;
  _DAT_ram_020d422c = 1;
  return 0;
}

