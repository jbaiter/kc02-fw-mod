/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009d5c Name: FUN_ram_02009d5c
 */

void FUN_ram_02009d5c(undefined4 param_1)

{
  undefined4 uVar1;
  bool bVar2;
  
  bVar2 = app_context.b5f == 0;
  if (bVar2) {
    uVar1 = win_find_child_by_id(0x11,&stack0x00000000);
  }
  else {
    uVar1 = win_find_child_by_id(0x11,&stack0x00000000);
    winSetResid(uVar1,0x8000004a);
    uVar1 = win_find_child_by_id(param_1,0x11);
  }
  winSetVisible(uVar1,!bVar2);
  return;
}

