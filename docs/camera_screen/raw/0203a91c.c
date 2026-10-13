/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203a91c Name: hal_lcdFrameEndCallBack
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: s32 hal_lcdFrameEndCallBack(void (*callback)(void))   [hal/inc/hal_lcd.h] */

int hal_lcdFrameEndCallBack(undefined4 param_1)

{
  undefined4 uVar1;
  
  if (*(sbyte *)(DAT_ram_020ce980 + 0x20) == 1) {
    uVar1 = 0x13;
  }
  else {
    uVar1 = 0x11;
  }
  ax32xx_lcdISRRegister(uVar1,param_1,&stack0x00000000);
  return 0;
}

