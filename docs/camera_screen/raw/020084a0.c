/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020084a0 Name: mode_photo_tick
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* pipeline map */

void mode_photo_tick(void)

{
  int iVar1;
  
  ui_record_indicator_timeout(_DAT_ram_020c2b48,&stack0x00000000);
  if (app_context.b5c == 0) {
    app_context.b5c = 1;
    iVar1 = FUN_ram_02022720();
    if (iVar1 != 0) {
      if (app_context.b59 == 2) {
        photo_apply_effect(*(undefined4 *)(iVar1 + 4),*(undefined4 *)(iVar1 + 8),
                           *(undefined2 *)(iVar1 + 0x20),*(undefined2 *)(iVar1 + 0x22));
      }
      else if (app_context.b59 == 3) {
        FUN_ram_020460b4(*(undefined4 *)(iVar1 + 4));
      }
      app_context.b5c = 2;
      hal_lcdDealBuffer(iVar1);
      lcd_commit_video_frame();
    }
  }
  return;
}

