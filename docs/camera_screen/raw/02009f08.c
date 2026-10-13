/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009f08 Name: kc02_power_handler
 */

/* POWER (0x24): ui_mode_request(6,0) + state 0x020D41D4/5 */

undefined4 kc02_power_handler(int param_1,int *param_2)

{
  if ((param_1 == 1) && (*param_2 == 0)) {
    ui_mode_request(6,&stack0x00000000);
    if (app_context.b68 == 0) {
      app_context.b69 = app_context.b68;
      app_context.b68 = 2;
    }
  }
  return 0;
}

