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

