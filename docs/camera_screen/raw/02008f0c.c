/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008f0c Name: FUN_ram_02008f0c
 */

void FUN_ram_02008f0c(undefined4 param_1)

{
  undefined4 uVar1;
  
  if (iRam020ccd98 == 0) {
    uVar1 = win_find_child_by_id(0x19,&stack0x00000000);
    winSetResid(uVar1,0x8000002a);
    uVar1 = win_find_child_by_id(param_1,0x1a);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,0x19);
    winSetVisible(uVar1,1);
  }
  if (iRam020ccd98 == 2) {
    uVar1 = win_find_child_by_id(param_1,0x19,register0x00000401);
    winSetResid(uVar1,0x8000002b);
    uVar1 = win_find_child_by_id(param_1,0x1a);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,0x19);
    winSetVisible(uVar1,1);
  }
  if (iRam020ccd98 == 1) {
    uVar1 = win_find_child_by_id(param_1,0x1a,register0x00000401);
    winSetResid(uVar1,0x80000026);
    uVar1 = win_find_child_by_id(param_1,0x19);
    winSetVisible(uVar1,0);
    uVar1 = win_find_child_by_id(param_1,0x1a);
    winSetVisible(uVar1,1);
  }
  return;
}

