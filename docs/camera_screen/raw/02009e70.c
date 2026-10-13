/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009e70 Name: FUN_ram_02009e70
 */

undefined4 FUN_ram_02009e70(undefined4 param_1)

{
  undefined4 uVar1;
  
  FUN_ram_02009bc0(&stack0x00000000);
  FUN_ram_0200917c(param_1);
  if (app_context.b4 == 0) {
    uVar1 = 0x81000059;
  }
  else if (app_context.b4 == 3) {
    uVar1 = 0x8100005a;
  }
  else {
    if (app_context.b4 != 2) {
      return 0;
    }
    uVar1 = 0x8100009e;
  }
  win_open(0x20c4434,uVar1,2);
  return 0;
}

