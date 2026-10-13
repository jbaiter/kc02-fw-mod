/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202990c Name: winSetResid
 */

/* SDK prototype: void winSetResid(winHandle hWin,resID id)   [sUI/winManage.h] */

void winSetResid(undefined4 param_1)

{
  undefined4 local_18 [3];
  undefined4 local_c;
  
  local_18[0] = 7;
  local_c = param_1;
  winSendMsg(local_18);
  return;
}

