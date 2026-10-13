/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020047b4 Name: dispLayerShow
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: int dispLayerShow(INT8U layer,INT32U addr,INT16U width,INT16U height,INT8U rotate)
   [ax32_platform_demo/inc/display.h] */

int dispLayerShow(sbyte param_1,undefined *param_2)

{
  if (param_1 == 0) {
    hal_lcdSetFrame(0,0,0,0,param_2,0);
  }
  return 0;
}

