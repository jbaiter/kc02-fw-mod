/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009ccc Name: FUN_ram_02009ccc
 */

void FUN_ram_02009ccc(undefined4 param_1,int param_2,undefined4 param_3,undefined4 param_4)

{
  undefined4 uVar1;
  
  if (param_2 == 0) {
    uVar1 = win_find_child_by_id(0x13,&stack0x00000000);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,0x14);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,0x15);
    winSetVisible(uVar1,0);
  }
  else {
    FUN_ram_02009c1c(param_3,param_4);
  }
  return;
}

