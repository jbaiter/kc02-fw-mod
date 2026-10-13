/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02004944 Name: ui_state_apply
 */

/* state==7 special; else table 0x0207B908[state] */

undefined4 ui_state_apply(uint param_1)

{
  param_1 = param_1 & 0xff;
  if (param_1 == 7) {
    hal_lcdSetPIPEnable(0);
  }
  else {
    if (5 < param_1) {
      return 0xffffffff;
    }
    (*(code *)ui_state_dispatch[param_1])(&stack0x00000000);
  }
  return 0;
}

