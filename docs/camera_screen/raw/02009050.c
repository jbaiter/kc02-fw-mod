/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02009050 Name: ui_show_number_label
 */

/* Track2 evidence-based naming (likely, names_02.tsv): Formats app_context.b8+1 into a 2-digit
   decimal string, sets it into window 0x16, shows the window and calls video_present(1).  [was
   FUN_ram_02009050] */

void ui_show_number_label(undefined4 param_1)

{
  uint uVar1;
  undefined4 uVar2;
  sbyte local_10;
  sbyte local_f;
  undefined1 local_e;
  
  uVar1 = (char)(app_context.b8 + 1) / 10;
  local_10 = (sbyte)(uVar1 % 10) + 0x30;
  local_f = app_context.b8 + 1 + (sbyte)uVar1 * -10 + 0x30;
  local_e = 0x58;
  uVar2 = win_find_child_by_id(0x16);
  winSetResid(uVar2,&local_10);
  uVar2 = win_find_child_by_id(param_1,0x16);
  winSetVisible(uVar2,1);
  video_present(1);
  return;
}

