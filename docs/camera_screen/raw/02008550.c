/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008550 Name: FUN_ram_02008550
 */

int FUN_ram_02008550(undefined4 *param_1)

{
  int iVar1;
  char *pcVar2;
  
  if (app_context.b4 == 4) {
    iVar1 = managerSpaceCheck(app_context._56_4_,s_PHOTO__ram_0207b859,0x1400);
    if ((iVar1 < 0) ||
       (pcVar2 = managerFileNameCreater(app_context._56_4_,&DAT_ram_0207b870,s_PHOTO__ram_0207b859),
       pcVar2 == (char *)0x0)) {
      app_context.b4 = 3;
      iVar1 = -2;
      XMsgQPost((undefined *)app_context._116_4_,(undefined *)0x1);
    }
    else {
      hal_sdLock();
      iVar1 = open(pcVar2,7,register0x00000401);
      if (param_1 != (undefined4 *)0x0) {
        *param_1 = pcVar2;
      }
    }
  }
  else {
    iVar1 = -1;
    XMsgQPost((undefined *)app_context._116_4_,(undefined *)0x1);
  }
  return iVar1;
}

