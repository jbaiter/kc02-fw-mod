/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02012304 Name: win_open
 */

/* Track2 evidence-based naming (likely, names_03.tsv): window open helper: looks up/reuses a window
   handle, registers the msg function, sends message 0x22 and returns the handle; used with several
   window descriptors across the UI.  [was FUN_ram_02012304] */

undefined4 win_open(undefined4 *param_1,uint param_2)

{
  uint *puVar1;
  uint uVar2;
  uint *puVar3;
  int iVar4;
  undefined4 local_38 [3];
  uint *local_2c;
  uint local_28;
  uint local_24 [5];
  
  if (5 < param_2) {
    param_2 = 5;
  }
  puVar1 = &local_28;
  puVar3 = (uint *)register0x00000401;
  for (uVar2 = 0; puVar1 = puVar1 + 1, uVar2 != param_2; uVar2 = uVar2 + 1) {
    *puVar1 = *puVar3;
    puVar3 = puVar3 + 1;
  }
  local_28 = param_2;
  if (param_1[2] == 0) {
    if (param_1[1] != 0) {
      *(undefined4 **)(param_1[1] + 0x24) = param_1;
    }
    iVar4 = win_create_widget_list(FUN_ram_02012150,0);
    if (iVar4 != 0) {
      msgFuncRegister((msgDealInfor *)*param_1);
      param_1[2] = iVar4;
      goto LAB_ram_020123b8;
    }
  }
  else if (*(sbyte *)(param_1 + 3) != 0) {
LAB_ram_020123b8:
    local_38[0] = 0x22;
    local_2c = &local_28;
    winSendMsg(param_1[2],local_38);
    return param_1[2];
  }
  return 0;
}

