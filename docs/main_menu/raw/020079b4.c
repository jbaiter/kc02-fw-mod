/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020079b4 Name: ui_key_dispatch
 */

/* UI key dispatcher (l.jalr r14 at 0x02007A98); r4=1, r5=&flags, ret 0x02007A9C */

void ui_key_dispatch(void)

{
  uint uVar1;
  bool bVar2;
  undefined *puVar3;
  undefined3 extraout_var;
  int iVar4;
  undefined4 uVar5;
  code *pcVar6;
  sbyte sStack_15;
  uint uStack_14;
  
  puVar3 = XMsgQPend((undefined *)app_context._116_4_,&sStack_15);
  if ((sStack_15 == 0) && (uVar1 = (uint)puVar3 & 0xffff, uVar1 < 0x2e)) {
    uStack_14 = (uint)puVar3 >> 0x10;
    if (((uVar1 - 0x1a < 0x14) && (uStack_14 == 0)) &&
       ((((bVar2 = menuIsOpen(), CONCAT31(extraout_var,bVar2) != 0 ||
          (iVar4 = ui_mode_get_current(), iVar4 != 4)) && (uVar1 != 0x26)) &&
        ((iVar4 = ui_mode_get_current(), iVar4 != 5 || (iVar4 = audio_core_state(), iVar4 != 1))))))
    {
      FUN_ram_0200ce14();
    }
    pcVar6 = (code *)(&DAT_ram_020ccc8c)[uVar1];
    if (pcVar6 != (code *)0x0) {
      uVar5 = ui_get_ctx();
      (*pcVar6)(uVar5,1,&uStack_14);
    }
    pcVar6 = *(code **)(&DAT_ram_020ccbd4 + uVar1 * 4);
    if (pcVar6 != (code *)0x0) {
      uVar5 = ui_get_ctx();
      (*pcVar6)(uVar5,1,&uStack_14);
    }
  }
  return;
}

