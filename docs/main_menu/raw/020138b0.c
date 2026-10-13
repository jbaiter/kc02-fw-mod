/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020138b0 Name: main_menu_page_down
 */

undefined4 main_menu_page_down(int param_1,int *param_2)

{
  sbyte sVar1;
  
  if ((param_1 == 1) && (*param_2 == 0)) {
    if (app_context.b67 < 3) {
      sVar1 = 3;
    }
    else {
      sVar1 = -3;
    }
    app_context.b67 = app_context.b67 + sVar1;
    uRam020cced0 = 0;
    FUN_ram_02013138(0,&stack0x00000000);
    uRam020cced4 = XOSTimeGet();
  }
  return 0;
}

