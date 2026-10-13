/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013938 Name: main_menu_ok
 */

/* menu item -> mode: 0->3, 1->2, 2->5, 3->4, 4->7, 5->9 */

undefined4 main_menu_ok(int param_1,int *param_2)

{
  byte bVar1;
  undefined4 uVar2;
  
  if ((param_1 == 1) && (*param_2 == 0)) {
    FUN_ram_020132e4(0,&stack0x00000000);
    video_present(1);
    uVar2 = 3;
    if ((app_context.b67 != 0) && (uVar2 = 2, app_context.b67 != 1)) {
      if (app_context.b67 == 2) {
        ui_mode_request(5,0);
        bVar1 = app_context.b68;
        if (app_context.b68 != 0) {
          return 0;
        }
        app_context.b68 = 1;
        app_context.b69 = bVar1;
        return 0;
      }
      uVar2 = 4;
      if ((app_context.b67 != 3) && (uVar2 = 7, app_context.b67 != 4)) {
        if (app_context.b67 != 5) {
          return 0;
        }
        uVar2 = 9;
      }
    }
    ui_mode_request(uVar2,0);
  }
  return 0;
}

