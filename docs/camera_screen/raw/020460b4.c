/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020460b4 Name: FUN_ram_020460b4
 */

void FUN_ram_020460b4(int param_1)

{
  undefined4 uVar1;
  int *piVar2;
  uint uVar3;
  
  uVar1 = JPEG_ram_020d7582._11706_4_;
  if (JPEG_ram_020d7582._11646_4_ == 3) {
    piVar2 = (int *)JPEG_ram_020d7582._11710_4_;
    for (uVar3 = 0; uVar3 < (uint)JPEG_ram_020d7582._11718_4_; uVar3 = uVar3 + 1) {
      ax32xx_sysInit((uint *)(param_1 + *piVar2),(uint *)(uVar1 + *piVar2));
      piVar2 = piVar2 + 2;
    }
  }
  return;
}

