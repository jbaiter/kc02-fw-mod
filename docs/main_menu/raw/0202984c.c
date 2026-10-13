/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202984c Name: winSetVisible
 */

/* SDK prototype: void winSetVisible(winHandle hWin,bool visible)   [sUI/winManage.h] */

void winSetVisible(undefined4 param_1,sbyte param_2)

{
  bool bVar1;
  undefined4 local_20 [3];
  int local_14;
  
  local_20[0] = 0xc;
  winSendMsg(local_20);
  if (param_2 == 0) {
    bVar1 = local_14 == 0;
    local_14 = 0;
    if (bVar1) {
      return;
    }
  }
  else {
    if (local_14 != 0) {
      return;
    }
    local_14 = 1;
  }
  local_20[0] = 0xb;
  winSendMsg(param_1,local_20);
  return;
}

