/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020283cc Name: FUN_ram_020283cc
 */

void FUN_ram_020283cc(int param_1,undefined4 param_2)

{
  int iVar1;
  
  iVar1 = func_0x02028310((int)*(short *)(param_1 + 6),(int)*(short *)(param_1 + 8),
                          *(undefined2 *)(param_1 + 10),*(undefined2 *)(param_1 + 0xc),param_2,
                          *(ushort *)(param_1 + 0xe) | 0x40,*(undefined2 *)(param_1 + 4),
                          &stack0x00000000);
  if (iVar1 != 0) {
    *(undefined4 *)(iVar1 + 0x38) = *(undefined4 *)(param_1 + 0x1c);
    *(undefined1 *)(iVar1 + 0x3d) = *(undefined1 *)(param_1 + 0x20);
    *(undefined1 *)(iVar1 + 0x3e) = *(undefined1 *)(param_1 + 0x21);
    *(undefined1 *)(iVar1 + 0x3c) = *(undefined1 *)(param_1 + 0x22);
    winSetfgColor(iVar1,*(undefined1 *)(param_1 + 0x10));
    *(undefined1 *)(iVar1 + 0x40) = *(undefined1 *)(param_1 + 0x29);
    *(undefined1 *)(iVar1 + 0x41) = *(undefined1 *)(param_1 + 0x28);
    *(undefined1 *)(iVar1 + 0x42) = *(undefined1 *)(param_1 + 0x2a);
    *(undefined1 *)(iVar1 + 0x43) = *(undefined1 *)(param_1 + 0x2b);
    *(undefined1 *)(iVar1 + 0x3f) = *(undefined1 *)(param_1 + 0x2d);
  }
  return;
}

