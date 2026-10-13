/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02007410 Name: FUN_ram_02007410
 */

undefined4 FUN_ram_02007410(int param_1,int param_2)

{
  int iVar1;
  undefined1 uStack_20;
  undefined1 uStack_1f;
  undefined2 uStack_1e;
  int iStack_1c;
  undefined2 uStack_18;
  int iStack_14;
  
  iVar1 = audioPlaybackGetStatus();
  if ((iVar1 != 1) && (iVar1 = ui_mode_get_current(), iVar1 != 1)) {
    if (param_1 == 0) {
      return 0xffffffff;
    }
    iStack_14 = param_1 + 0x2c;
    uStack_1f = 1;
    uStack_20 = 6;
    uStack_1e = 16000;
    iStack_1c = param_2 + -0x2c;
    uStack_18 = 2;
    func_0x020525b4(app_context.b6b * 10);
    func_0x020521d0(&uStack_20);
  }
  return 0;
}

