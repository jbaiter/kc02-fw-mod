/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202a110 Name: winUpdate
 */

/* SDK prototype: void winUpdate(winHandle hWin)   [sUI/winManage.h] */

void winUpdate(undefined2 *param_1)

{
  if (param_1 != (undefined2 *)0x0) {
    if ((param_1[0xd] & 1) == 0) {
      DAT_ram_020ce0d0 = DAT_ram_020ce0d0 + 1;
    }
    param_1[6] = *param_1;
    param_1[7] = param_1[1];
    param_1[8] = param_1[2];
    param_1[9] = param_1[3];
    param_1[0xd] = param_1[0xd] | 1;
  }
  return;
}

