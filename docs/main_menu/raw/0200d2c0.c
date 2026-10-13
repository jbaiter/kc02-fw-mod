/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200d2c0 Name: system_tick
 */

/* 10 ms gated tasks (incl. kc02_keypad_poll) + usb/charger tick */

void system_tick(int param_1)

{
  int iVar1;
  
  timerMsgSend(&stack0x00000000);
  hal_ispService();
  if (param_1 != 0) {
    kc02_keypad_poll();
  }
  iVar1 = XOSTimeGet();
  if (10 < (uint)(iVar1 - DAT_ram_020ccde8)) {
    DAT_ram_020ccde8 = XOSTimeGet();
    iVar1 = ui_mode_get_current();
    if (iVar1 != 1) {
      tick_sd_detect();
      tick_lcd_timeout(0);
      tick_auto_poweroff(0);
    }
    tick_usb_charger();
  }
  return;
}

