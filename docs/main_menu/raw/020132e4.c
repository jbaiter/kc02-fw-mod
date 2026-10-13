/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020132e4 Name: FUN_ram_020132e4
 */

void FUN_ram_020132e4(undefined4 param_1,int param_2)

{
  undefined4 uVar1;
  
  if (param_2 == 0) {
    uVar1 = win_find_child_by_id(&stack0x00000000);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,1);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,2);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,3);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,4);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,5);
  }
  else {
    uVar1 = win_find_child_by_id(0,&stack0x00000000);
    winSetVisible(uVar1,1);
    uVar1 = win_find_child_by_id(param_1,1);
    winSetVisible(uVar1,1);
    uVar1 = win_find_child_by_id(param_1,2);
    winSetVisible(uVar1,1);
    uVar1 = win_find_child_by_id(param_1,3);
    winSetVisible(uVar1,1);
    uVar1 = win_find_child_by_id(param_1,4);
    winSetVisible(uVar1,1);
    uVar1 = win_find_child_by_id(param_1,5);
  }
  winSetVisible(uVar1,param_2 != 0);
  return;
}

