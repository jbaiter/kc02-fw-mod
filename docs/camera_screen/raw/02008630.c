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

