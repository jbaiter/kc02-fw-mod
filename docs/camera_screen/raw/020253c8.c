/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020253c8 Name: frameWinCreateIndirect
 */

/* SDK prototype: winHandle frameWinCreateIndirect(widgetCreateInfor* infor,winHandle parent,winCB
   cb)   [sUI/frameWin.h] */

void frameWinCreateIndirect(int p0,undefined4 param_2,undefined4 param_3)

{
  int iVar1;
  
  iVar1 = frameWinCreate((int)*(short *)(p0 + 6),(int)*(short *)(p0 + 8),*(undefined2 *)(p0 + 10),
                         *(undefined2 *)(p0 + 0xc),param_2,*(undefined2 *)(p0 + 0xe),param_3,
                         &stack0x00000000);
  winSetfgColor(iVar1,*(undefined1 *)(p0 + 0x10));
  if (iVar1 != 0) {
    *(undefined4 *)(iVar1 + 0x84) = *(undefined4 *)(p0 + 0x24);
  }
  return;
}

