/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02007880 Name: msgFuncRegister
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void msgFuncRegister(msgDealInfor* infor)   [ax32_platform_demo/application.h] */

void msgFuncRegister(msgDealInfor *p0)

{
  int iVar1;
  undefined4 *puVar2;
  kc02_task_map_entry *pkVar3;
  
  iVar1 = 0;
  do {
    puVar2 = (undefined4 *)((int)&DAT_ram_020ccc8c + iVar1);
    iVar1 = iVar1 + 4;
    *puVar2 = 0;
  } while (iVar1 != 0xb8);
  pkVar3 = task_map_builtin;
  while (pkVar3 = pkVar3 + 1, pkVar3->msg < 0x2e) {
    if (pkVar3->handler == (undefined *)0x0) break;
    (&DAT_ram_020ccc8c)[pkVar3->msg] = pkVar3->handler;
  }
  if (p0 != (msgDealInfor *)0x0) {
    while (p0->msgId < 0x2e) {
      if (p0->func == (undefined *)0x0) {
        return;
      }
      (&DAT_ram_020ccc8c)[p0->msgId] = p0->func;
      p0 = p0 + 1;
    }
  }
  return;
}

