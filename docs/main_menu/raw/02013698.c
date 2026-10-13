/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013698 Name: ui_toggle_state_300ticks
 */

/* Track2b second-opinion revise (names_02.tsv): XOSTimeGet is compared against iRam020cced4+300,
   then sRam020cced0 is boolean-toggled and passed to func_0x02013138. The body establishes a
   300-tick state toggle, not milliseconds or an indicator.  [was FUN_ram_02013698] */

undefined4 ui_toggle_state_300ticks(void)

{
  uint uVar1;
  
  uVar1 = XOSTimeGet();
  if (iRam020cced4 + 300U <= uVar1) {
    sRam020cced0 = sRam020cced0 == 0;
    FUN_ram_02013138(sRam020cced0,register0x00000401);
    iRam020cced4 = XOSTimeGet();
  }
  return 0;
}

