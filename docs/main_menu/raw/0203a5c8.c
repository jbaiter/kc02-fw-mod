/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203a5c8 Name: display_get_mode
 */

/* SDK align: src_hal_lcd.c.o/hal_lcdGetResolution score=1.0 segment */

undefined4 display_get_mode(undefined2 *param_1,undefined2 *param_2)

{
  int iVar1;
  undefined2 uVar2;
  
  iVar1 = DAT_ram_020ce980;
  if (DAT_ram_020ce980 == 0) {
    return 0xffffffff;
  }
  if (DAT_ram_020ce961 == 0) {
    if (param_1 != (undefined2 *)0x0) {
      *param_1 = *(undefined2 *)(DAT_ram_020ce980 + 0x34);
    }
    if (param_2 == (undefined2 *)0x0) {
      return 0;
    }
    uVar2 = *(undefined2 *)(iVar1 + 0x36);
  }
  else {
    if (param_1 != (undefined2 *)0x0) {
      *param_1 = *(undefined2 *)(DAT_ram_020ce980 + 0x36);
    }
    if (param_2 == (undefined2 *)0x0) {
      return 0;
    }
    uVar2 = *(undefined2 *)(iVar1 + 0x34);
  }
  *param_2 = uVar2;
  return 0;
}

