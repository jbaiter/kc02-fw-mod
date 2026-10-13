/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009bc0 Name: FUN_ram_02009bc0
 */

void FUN_ram_02009bc0(void)

{
  undefined4 uVar1;
  undefined4 uVar2;
  
  if (app_context.b4 == 0) {
    uVar2 = win_find_child_by_id(8,&stack0x00000000);
    uVar1 = 0x8000004d;
  }
  else {
    uVar2 = win_find_child_by_id(8,&stack0x00000000);
    uVar1 = 0x8000004c;
  }
  winSetResid(uVar2,uVar1);
  return;
}

