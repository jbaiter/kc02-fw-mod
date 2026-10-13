/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008bec Name: FUN_ram_02008bec
 */

void FUN_ram_02008bec(undefined4 param_1)

{
  sbyte sVar1;
  int iVar2;
  undefined4 uVar3;
  
  if (app_context.b2 != 0) {
    sVar1 = app_context.b6a + 1;
    app_context.b6a = app_context.b6a + 1;
    if ('\x03' < (char)sVar1) {
      app_context.b6a = 0;
    }
    uVar3 = win_find_child_by_id(param_1,10,&stack0x00000000);
    iVar2 = app_context.b6a + 0x80000022;
    goto LAB_ram_02008d08;
  }
  if (app_context.b0 != 4) {
    if (app_context.b0 == 3) {
      uVar3 = win_find_child_by_id(10,&stack0x00000000);
      iVar2 = -0x7fffffdc;
      goto LAB_ram_02008d08;
    }
    if (app_context.b0 == 2) {
      uVar3 = win_find_child_by_id(10,&stack0x00000000);
      iVar2 = -0x7fffffdd;
      goto LAB_ram_02008d08;
    }
    if (app_context.b0 == 1) {
      uVar3 = win_find_child_by_id(10,&stack0x00000000);
      iVar2 = -0x7fffffde;
      goto LAB_ram_02008d08;
    }
    if (app_context.b0 == 0) {
      uVar3 = win_find_child_by_id(10,&stack0x00000000);
      iVar2 = -0x7fffffdf;
      goto LAB_ram_02008d08;
    }
  }
  uVar3 = win_find_child_by_id(param_1,10,&stack0x00000000);
  iVar2 = -0x7fffffdb;
LAB_ram_02008d08:
  winSetResid(uVar3,iVar2);
  uVar3 = win_find_child_by_id(param_1,10,register0x00000401);
  winSetVisible(uVar3,1);
  return;
}

