/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0201379c Name: main_menu_prev_item
 */

undefined4 main_menu_prev_item(int param_1,int *param_2)

{
  if ((param_1 == 1) && (*param_2 == 0)) {
    if (app_context.b67 == 0) {
      app_context.b67 = 5;
    }
    else {
      app_context.b67 = app_context.b67 - 1;
    }
    uRam020cced0 = 0;
    FUN_ram_02013138(0,&stack0x00000000);
    uRam020cced4 = XOSTimeGet();
  }
  return 0;
}

