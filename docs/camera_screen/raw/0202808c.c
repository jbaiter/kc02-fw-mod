/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202808c Name: rectCreateIndirect
 */

/* SDK prototype: winHandle rectCreateIndirect(widgetCreateInfor* infor,winHandle parent,winCB cb)
   [sUI/rect.h] */

int rectCreateIndirect(int p0,undefined4 param_2)

{
  int iVar1;
  
  iVar1 = rectCreate((int)*(short *)(p0 + 6),(int)*(short *)(p0 + 8),*(undefined2 *)(p0 + 10),
                     *(undefined2 *)(p0 + 0xc),param_2,*(ushort *)(p0 + 0xe) | 0x40,
                     *(undefined2 *)(p0 + 4),&stack0x00000000);
  if (iVar1 != 0) {
    *(undefined1 *)(iVar1 + 0x38) = *(undefined1 *)(p0 + 0x21);
    winSetfgColor(iVar1,*(undefined1 *)(p0 + 0x10));
  }
  return iVar1;
}

