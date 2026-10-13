/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02028b04 Name: winSendMsg
 */

/* SDK prototype: void winSendMsg(winHandle handle,winMsg* msg)   [sUI/winManage.h] */

void winSendMsg(int param_1,int param_2)

{
  code *pcVar1;
  
  if ((param_1 != 0) && (pcVar1 = *(code **)(param_1 + 0x20), pcVar1 != (code *)0x0)) {
    *(int *)(param_2 + 4) = param_1;
    (*pcVar1)(param_2,&stack0x00000000);
  }
  return;
}

