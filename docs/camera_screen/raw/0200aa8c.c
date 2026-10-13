/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200aa8c Name: ui_record_indicator_timeout
 */

/* Track2 evidence-based naming (likely, names_02.tsv): When the record indicator state
   app_context.b6d==2 and 800 time units have elapsed since DAT_ram_020ccda4, clears b6d and
   refreshes indicator and label.  [was FUN_ram_0200aa8c] */

void ui_record_indicator_timeout(undefined4 param_1)

{
  int iVar1;
  
  if ((app_context.b6d == 2) && (iVar1 = XOSTimeGet(), 800 < (uint)(iVar1 - DAT_ram_020ccda4))) {
    app_context.b6d = 0;
    ui_show_record_indicator(param_1,register0x00000401);
    ui_show_number_label(param_1);
  }
  return;
}

