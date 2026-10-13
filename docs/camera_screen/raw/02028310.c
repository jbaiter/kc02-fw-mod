/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02028310 Name: FUN_ram_02028310
 */

void FUN_ram_02028310(short param_1,uint param_2,undefined2 param_3)

{
  undefined2 uVar1;
  uint uVar2;
  int iVar3;
  undefined4 uVar4;
  
  uVar1 = param_3;
  uVar2 = param_2 & 0xffff | 2;
  uVar4 = 0x48;
  iVar3 = winTouch((uiRect *)(int)param_1);
  widgetSetId(iVar3,uVar1,uVar4,uVar2,register0x00000401);
  if (iVar3 != 0) {
    *(undefined1 *)(iVar3 + 0x3e) = 0xfb;
    *(undefined4 *)(iVar3 + 0x38) = 0x7fffffff;
    *(undefined1 *)(iVar3 + 0x3f) = 0xff;
    *(undefined1 *)(iVar3 + 0x43) = 0xff;
    *(undefined1 *)(iVar3 + 0x44) = 0xff;
    winSetfgColor(iVar3,0xff);
    *(undefined1 *)(iVar3 + 0x45) = 0;
  }
  return;
}

