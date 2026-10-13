/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020130e4 Name: mode_menu_enter
 */

/* mode 6 Main menu */

void mode_menu_enter(void)

{
  ax32xx_csiEnable(0);
  ui_state_apply(7,register0x00000401);
  win_open(&PTR_mode6_main_menu_keymap_ram_020c4c2c,0);
  return;
}

