/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020134b8 Name: main_menu_init_icons
 */

/* six icons from 0x0207BB78 */

undefined4 main_menu_init_icons(undefined4 param_1)

{
  undefined4 *puVar1;
  undefined *p0;
  undefined *puVar2;
  int iVar3;
  
  uRam020cced0 = 0;
  if (5 < app_context.b67) {
    app_context.b67 = 0;
  }
  display_get_mode(0x20cceca,0x20ccec8,&stack0x00000000);
  puRam020ccecc = hal_sysMemMalloc((int)((uint)uRam020cceca * (uint)uRam020ccec8 * 3) >> 1,0x20);
  if (puRam020ccecc != (undefined *)0x0) {
    photo_jpeg_decode_to_yuv
              (0x20,puRam020ccecc,puRam020ccecc + (uint)uRam020cceca * (uint)uRam020ccec8);
    uRam020cceae = 0x60;
    uRam020cceac = 0x60;
    p0 = hal_sysMemMalloc(0x6c00,0x20);
    if (p0 != (undefined *)0x0) {
      iVar3 = 0;
      do {
        puVar2 = hal_sysMemMalloc((int)((uint)uRam020cceae * (uint)uRam020cceac * 3) >> 1,0x20);
        *(undefined **)(iVar3 + 0x20cceb0) = puVar2;
        if (puVar2 == (undefined *)0x0) {
          return 0;
        }
        puVar1 = (undefined4 *)(&main_menu_icon_resources + iVar3);
        iVar3 = iVar3 + 4;
        icon_decode(*puVar1,p0,puVar2,puVar2 + (uint)uRam020cceae * (uint)uRam020cceac);
      } while (iVar3 != 0x18);
      hal_sysMemFree(p0);
      FUN_ram_02013138(uRam020cced0);
      uRam020cced4 = XOSTimeGet();
      FUN_ram_020132e4(param_1,1);
    }
  }
  return 0;
}

