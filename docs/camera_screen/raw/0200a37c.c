/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200a37c Name: FUN_ram_0200a37c
 */

undefined4 FUN_ram_0200a37c(undefined4 param_1)

{
  undefined4 uVar1;
  
  iRam020ccd98 = 0;
  func_0x020075e8(&stack0x00000000);
  FUN_ram_02009ccc(param_1,0,0);
  FUN_ram_0200a030(param_1);
  FUN_ram_02009bc0(param_1);
  FUN_ram_02008bec(param_1);
  FUN_ram_02008d98(param_1);
  FUN_ram_02009358(param_1,0);
  FUN_ram_02009d5c(param_1);
  FUN_ram_0200917c(param_1);
  UNK_ram_020c8780 = 1;
  ui_show_record_indicator(param_1);
  if (iRam020ccda0 == 0) {
    iRam020ccda0 = func_0x02007744(0x28,0x20ccd9c);
  }
  if (iRam020ccd98 == 0) {
    uVar1 = win_find_child_by_id(param_1,0x1a);
    winSetVisible(uVar1,0);
  }
  FUN_ram_02008f0c(param_1);
  if (app_context.b68 == 0) {
    app_context.b69 = app_context.b68;
    app_context.b68 = 1;
  }
  func_0x02077cf4(0x10,0x20,0xd);
  return 0;
}

