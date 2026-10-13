/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202b0d4 Name: ax32xx_sysDcacheFlush
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void ax32xx_sysDcacheFlush(u32 addr, u32 size)   [ax32xx/inc/ax32xx_sys.h] */

void ax32xx_sysDcacheFlush(uint p0,uint p1)

{
  uint uVar1;
  
  if (p0 - 0x44000 < 0x7ffbc000) {
    while (p1 != 0) {
      uVar1 = p1;
      if (0x1000 < p1) {
        uVar1 = 0x1000;
      }
      p1 = p1 - uVar1;
      Spr0000181c = ((uVar1 + p0) - (p0 & 0xfffffff0)) - 1 >> 4;
      Spr00001814 = p0;
      p0 = uVar1 + p0;
    }
  }
  return;
}

