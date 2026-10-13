/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008d98 Name: FUN_ram_02008d98
 */

void FUN_ram_02008d98(undefined4 param_1)

{
  byte bVar1;
  undefined4 uVar2;
  undefined4 uVar3;
  
  bVar1 = app_context.b5b;
  if (app_context.b5b != 0) {
    if (app_context.b5b != 1) {
      if (app_context.b5b == 2) {
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000040;
      }
      else if (app_context.b5b == 3) {
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000046;
      }
      else if (app_context.b5b == 4) {
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000047;
      }
      else if (app_context.b5b == 5) {
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000045;
      }
      else if (app_context.b5b == 6) {
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000048;
      }
      else if (app_context.b5b == 7) {
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000043;
      }
      else {
        if (app_context.b5b != 8) {
          return;
        }
        uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
        uVar2 = 0x80000044;
      }
      winSetResid(uVar3,uVar2);
      uVar3 = win_find_child_by_id(param_1,0xf,register0x00000401);
      bVar1 = 1;
      goto LAB_ram_02008ef0;
    }
    uVar3 = win_find_child_by_id(0xf,&stack0x00000000);
    winSetResid(uVar3,0x8000003f);
  }
  uVar3 = win_find_child_by_id(param_1,0xf,register0x00000401);
LAB_ram_02008ef0:
  winSetVisible(uVar3,bVar1);
  return;
}

