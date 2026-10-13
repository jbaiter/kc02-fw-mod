/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02000404 Name: app_main_loop
 */

/* per-frame: sys_irq_clear, system_tick(1), ui_key_dispatch, video_present */

void app_main_loop(void)

{
  sys_irq_clear(&stack0x00000000);
  system_tick(1);
  ui_key_dispatch();
  video_present(0);
  return;
}

