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

