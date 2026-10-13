/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020075e8 Name: FUN_ram_020075e8
 */

undefined4 FUN_ram_020075e8(void)

{
  int iVar1;
  
  iVar1 = 0;
  while( true ) {
    if ((-1 < (int)-(uint)*(char *)(iVar1 + 0x207b99f)) && (iVar1 >= 0x1f)) break;
    *(char *)(iVar1 + 0x20d68cc) = *(char *)(iVar1 + 0x207b99f);
    iVar1 = iVar1 + 1;
  }
  *(char *)(iVar1 + 0x20d68cc) = iVar1 < 0x1f;
  return 0x20d68cc;
}

