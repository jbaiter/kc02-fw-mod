/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009c1c Name: FUN_ram_02009c1c
 */

void FUN_ram_02009c1c(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  undefined4 uVar1;
  
  uVar1 = win_find_child_by_id(0x13,&stack0x00000000);
  winSetVisible(uVar1,1);
  uVar1 = win_find_child_by_id(param_1,0x14);
  winSetResid(uVar1,param_2);
  uVar1 = win_find_child_by_id(param_1,0x14);
  winSetVisible(uVar1,1);
  uVar1 = win_find_child_by_id(param_1,0x15);
  winSetResid(uVar1,param_3);
  uVar1 = win_find_child_by_id(param_1,0x15);
  winSetVisible(uVar1,1);
  return;
}

