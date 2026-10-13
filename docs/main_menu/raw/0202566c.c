/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202566c Name: imageIconCreateIndirect
 */

/* SDK prototype: winHandle imageIconCreateIndirect(widgetCreateInfor* infor,winHandle parent,winCB
   cb)   [sUI/imageIcon.h] */

int imageIconCreateIndirect(int p0,undefined4 param_2)

{
  int iVar1;
  
  iVar1 = imageIconCreate((int)*(short *)(p0 + 6),(int)*(short *)(p0 + 8),*(undefined2 *)(p0 + 10),
                          *(undefined2 *)(p0 + 0xc),param_2,*(ushort *)(p0 + 0xe) | 0x40,
                          *(undefined2 *)(p0 + 4),&stack0x00000000);
  if (iVar1 != 0) {
    *(undefined4 *)(iVar1 + 0x38) = *(undefined4 *)(p0 + 0x14);
    *(undefined1 *)(iVar1 + 0x3c) = *(undefined1 *)(p0 + 0x18);
    winSetfgColor(iVar1,*(undefined1 *)(p0 + 0x10));
  }
  return iVar1;
}

