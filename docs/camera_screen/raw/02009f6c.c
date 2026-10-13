/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009f6c Name: ui_key_cycle_state
 */

/* Track2b second-opinion confirm (names_01b.tsv): The param_2 == 1 and *param_3 == 0 guard cycles
   iRam020ccd98 through 0, 1, and 2, with boardIoctrl command 0x20000011 on transitions to 0 and 2.
   [was FUN_ram_02009f6c] */

undefined4 ui_key_cycle_state(undefined4 param_1,int param_2,int *param_3)

{
  if ((param_2 == 1) && (*param_3 == 0)) {
    if (iRam020ccd98 == 0) {
      iRam020ccd98 = 1;
    }
    else if (iRam020ccd98 == 1) {
      iRam020ccd98 = 2;
    }
    else {
      iRam020ccd98 = *param_3;
      boardIoctrl((int)(sbyte)app_context.b10,0x20000011,&stack0x00000000);
    }
    if (iRam020ccd98 == 2) {
      boardIoctrl((int)(sbyte)app_context.b10,0x20000011,1,register0x00000401);
    }
    FUN_ram_02008f0c(param_1);
  }
  return 0;
}

