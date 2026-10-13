/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020047f8 Name: dispLayerGetFrame
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void *dispLayerGetFrame(INT8U layer)   [ax32_platform_demo/inc/display.h] */

undefined * dispLayerGetFrame(uint param_1)

{
  undefined *puVar1;
  
  puVar1 = (undefined *)0x0;
  if ((param_1 & 0xff) < 3) {
    if ((param_1 & 0xff) - 1 < 2) {
      puVar1 = (undefined *)lcd_osd_get_show_addr(0,&stack0x00000000);
    }
    else {
      puVar1 = hal_lcdGetIdleBuffer();
    }
  }
  return puVar1;
}

