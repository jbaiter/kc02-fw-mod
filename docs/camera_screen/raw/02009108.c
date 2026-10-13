/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009108 Name: ui_show_rtc_clock
 */

/* Track2 evidence-based naming (likely, names_02.tsv): Reads the RTC via
   hal_rtcTimeGet/hal_rtcTime2String, sets the string into window 0xc and refreshes the screen when
   app_context.b2==1.  [was FUN_ram_02009108] */

undefined4 ui_show_rtc_clock(undefined4 param_1)

{
  undefined4 uVar1;
  undefined *p0;
  char *pcVar2;
  
  uVar1 = win_find_child_by_id(0xc,&stack0x00000000);
  p0 = hal_rtcTimeGet();
  pcVar2 = hal_rtcTime2String(p0);
  winSetResid(uVar1,pcVar2);
  if (app_context.b2 == 1) {
    FUN_ram_02008bec(param_1);
  }
  return 0;
}

