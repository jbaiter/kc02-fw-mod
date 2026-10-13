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

