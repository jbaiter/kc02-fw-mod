/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02034ee4 Name: ax32xx_csiEnable
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void ax32xx_csiEnable(u8 en)   [ax32xx/inc/ax32xx_csi.h] */

void ax32xx_csiEnable(byte p0)

{
  uint uVar1;
  
  if (p0 == 0) {
    uVar1 = Spr0000b014;
    Spr0000b014 = uVar1 & 0x7fffffff;
    uVar1 = Spr0000b000;
    Spr0000b000 = uVar1 & 0xfffffffe;
  }
  else {
    uVar1 = Spr0000b014;
    Spr0000b014 = uVar1 | 0x80000000;
    uVar1 = Spr0000b000;
    Spr0000b000 = uVar1 | 1;
  }
  ax32xx_intEnable(0xe,p0 != 0);
  return;
}

