/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02078188 Name: FUN_ram_02078188
 */

void FUN_ram_02078188(void)

{
  undefined4 *puVar1;
  int iVar2;
  
  puVar1 = (undefined4 *)0x20d68ec;
  for (iVar2 = 0; iVar2 < (int)(uint)cRam020d40fc; iVar2 = iVar2 + 1) {
    if ((undefined *)*puVar1 != (undefined *)0x0) {
      hal_sysMemFree((undefined *)*puVar1);
      *puVar1 = 0;
    }
    puVar1 = puVar1 + 1;
  }
  return;
}

