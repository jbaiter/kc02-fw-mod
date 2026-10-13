/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013714 Name: main_menu_next_item
 */

/* menu item idx 0..5 wrap */

undefined4 main_menu_next_item(int param_1,int *param_2)

{
  if ((param_1 == 1) && (*param_2 == 0)) {
    if (app_context.b67 < 5) {
      app_context.b67 = app_context.b67 + 1;
    }
    else {
      app_context.b67 = 0;
    }
    uRam020cced0 = 0;
    FUN_ram_02013138(0,&stack0x00000000);
    uRam020cced4 = XOSTimeGet();
  }
  return 0;
}

