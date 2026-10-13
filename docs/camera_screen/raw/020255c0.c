/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020255c0 Name: imageIconCreate
 */

/* SDK prototype: winHandle imageIconCreate(int16 x0,int16 y0,uint16 width,uint16 height,winHandle
   parent,uint16 style,uint16 id)   [sUI/imageIcon.h] */

int imageIconCreate(short param_1,uint param_2,undefined2 param_3)

{
  undefined2 uVar1;
  uint uVar2;
  int iVar3;
  undefined4 uVar4;
  
  uVar1 = param_3;
  uVar2 = param_2 & 0xffff | 2;
  uVar4 = 0x40;
  iVar3 = winTouch((uiRect *)(int)param_1);
  widgetSetId(iVar3,uVar1,uVar4,uVar2,register0x00000401);
  if (iVar3 != 0) {
    *(undefined1 *)(iVar3 + 0x3d) = 1;
    *(undefined4 *)(iVar3 + 0x38) = 0x7fffffff;
    *(undefined1 *)(iVar3 + 0x3c) = 2;
    winSetfgColor(iVar3,0xff);
  }
  return iVar3;
}

