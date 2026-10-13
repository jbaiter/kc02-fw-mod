/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200917c Name: FUN_ram_0200917c
 */

void FUN_ram_0200917c(undefined4 param_1)

{
  uint uVar1;
  uint uVar2;
  uint uVar3;
  undefined4 uVar4;
  sbyte sStack_20;
  sbyte sStack_1f;
  sbyte sStack_1e;
  sbyte sStack_1d;
  sbyte sStack_1c;
  sbyte sStack_1b;
  
  uVar4 = app_context._76_4_;
  uVar2 = setting_map_config_index(0x18);
  uVar3 = setting_map_config_index(0x18);
  uVar1 = 0;
  if (app_context.b4 == 4) {
    uVar2 = (uVar2 >> 0x10) * (uVar3 & 0xffff);
    uVar1 = 0x78;
    if ((((0xe1000 < uVar2) && (uVar1 = 0x104, 0x384000 < uVar2)) &&
        (uVar1 = 0x154, 0x57e400 < uVar2)) && (uVar1 = 400, 0x7d3e80 < uVar2)) {
      uVar1 = 500;
    }
    uVar1 = (uint)uVar4 / uVar1;
  }
  uVar2 = (uVar1 / 100000) / 10;
  sStack_20 = ((sbyte)(uVar1 / 100000) - ((sbyte)uVar2 * 2 + (sbyte)(uVar2 << 3))) + 0x30;
  uVar2 = (uVar1 / 10000) / 10;
  sStack_1f = ((sbyte)(uVar1 / 10000) - ((sbyte)uVar2 * 2 + (sbyte)(uVar2 << 3))) + 0x30;
  uVar2 = (uVar1 / 1000) / 10;
  sStack_1e = ((sbyte)(uVar1 / 1000) - ((sbyte)uVar2 * 2 + (sbyte)(uVar2 << 3))) + 0x30;
  uVar2 = (uVar1 / 100) / 10;
  sStack_1d = ((sbyte)(uVar1 / 100) - ((sbyte)uVar2 * 2 + (sbyte)(uVar2 << 3))) + 0x30;
  uVar2 = uVar1 / 10;
  sStack_1c = ((sbyte)uVar2 - ((sbyte)(uVar2 / 10) * 2 + (sbyte)(uVar2 / 10 << 3))) + 0x30;
  sStack_1b = ((sbyte)uVar1 - ((sbyte)uVar2 * 2 + (sbyte)(uVar2 << 3))) + 0x30;
  uVar4 = win_find_child_by_id(param_1,0x18);
  winSetResid(uVar4,&sStack_20);
  uVar4 = win_find_child_by_id(param_1,0x18);
  winSetVisible(uVar4,1);
  video_present(1);
  return;
}

