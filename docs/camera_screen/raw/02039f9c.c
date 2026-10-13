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

