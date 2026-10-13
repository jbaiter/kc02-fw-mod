/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013434 Name: main_menu_free_icons
 */

undefined4 main_menu_free_icons(void)

{
  int iVar1;
  undefined *p0;
  
  if (puRam020ccecc != (undefined *)0x0) {
    hal_sysMemFree(puRam020ccecc);
    puRam020ccecc = (undefined *)0x0;
  }
  iVar1 = 0;
  do {
    p0 = *(undefined **)(iVar1 + 0x20cceb0);
    if (p0 != (undefined *)0x0) {
      hal_sysMemFree(p0);
      *(undefined4 *)(iVar1 + 0x20cceb0) = 0;
    }
    iVar1 = iVar1 + 4;
  } while (iVar1 != 0x18);
  return 0;
}

