/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02012150 Name: FUN_ram_02012150
 */

void FUN_ram_02012150(uint *param_1)

{
  uint uVar1;
  undefined4 uVar2;
  uint uVar3;
  int iVar4;
  undefined4 uVar5;
  undefined4 *puVar6;
  undefined4 uStack_20;
  undefined4 uStack_1c;
  undefined4 uStack_18;
  
  uVar3 = *param_1;
  puVar6 = &uStack_20;
  uVar1 = param_1[1];
  if (uVar3 == 0x15) {
    uVar3 = param_1[3];
    uVar2 = 3;
    uStack_20 = *(undefined4 *)(uVar3 + 0x14);
    uStack_1c = *(undefined4 *)(uVar3 + 0x18);
    uStack_18 = *(undefined4 *)(uVar3 + 0x1c);
    uVar5 = 0x12;
LAB_ram_020122b4:
    ui_key_invoke(uVar5,uVar1,uVar2,puVar6);
  }
  else {
    if (uVar3 < 0x16) {
      if (uVar3 == 2) {
        if (*(undefined4 **)(uVar1 + 0x84) == (undefined4 *)0x0) {
          return;
        }
        msgFuncRegister((msgDealInfor *)**(undefined4 **)(uVar1 + 0x84));
        uVar2 = 0;
        uVar5 = 0x10;
        puVar6 = (undefined4 *)0x0;
        goto LAB_ram_020122b4;
      }
      if (uVar3 != 4) {
        return;
      }
      uVar5 = 0;
      uVar2 = **(undefined4 **)(uVar1 + 0x84);
      uVar3 = param_1[3];
      puVar6 = (undefined4 *)0x0;
    }
    else {
      if (uVar3 != 0x22) {
        if (uVar3 != 0x23) {
          return;
        }
        puVar6 = *(undefined4 **)(uVar1 + 0x84);
        if (puVar6 == (undefined4 *)0x0) {
          return;
        }
        msgDealByMsgInfor(*puVar6,0xf,uVar1,0);
        puVar6[2] = 0;
        return;
      }
      puVar6 = *(undefined4 **)(uVar1 + 0x84);
      if (puVar6 == (undefined4 *)0x0) {
        return;
      }
      iVar4 = *(int *)(uVar1 + 0x24);
      if (((iVar4 != 0) && ((*(ushort *)(iVar4 + 0x1a) & 0x10) != 0)) &&
         (*(undefined4 **)(iVar4 + 0x84) != (undefined4 *)0x0)) {
        msgDealByMsgInfor(**(undefined4 **)(iVar4 + 0x84),0x11,0);
      }
      uVar2 = *puVar6;
      uVar5 = *(undefined4 *)param_1[3];
      uVar3 = 0xe;
      puVar6 = (undefined4 *)param_1[3] + 1;
    }
    msgDealByMsgInfor(uVar2,uVar3,uVar1,uVar5,puVar6);
  }
  return;
}

