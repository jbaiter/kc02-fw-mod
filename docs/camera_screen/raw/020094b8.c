/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020094b8 Name: ui_show_record_indicator
 */

/* Track2 evidence-based naming (likely, names_02.tsv): Shows/hides window 0x17 and selects
   indicator resource 0x80000042 or 0x80000041 from the record state app_context.b6d.  [was
   FUN_ram_020094b8] */

void ui_show_record_indicator(undefined4 param_1)

{
  undefined1 uVar1;
  undefined4 uVar2;
  undefined4 uVar3;
  
  if (app_context.b6d == 0) {
    uVar3 = win_find_child_by_id(0x17,&stack0x00000000);
    uVar1 = 0;
    goto LAB_ram_0200954c;
  }
  if (app_context.b6d == 1) {
    uVar3 = win_find_child_by_id(0x17,&stack0x00000000);
    uVar2 = 0x80000042;
LAB_ram_02009534:
    winSetResid(uVar3,uVar2);
  }
  else if (app_context.b6d == 2) {
    uVar3 = win_find_child_by_id(0x17,&stack0x00000000);
    uVar2 = 0x80000041;
    goto LAB_ram_02009534;
  }
  uVar3 = win_find_child_by_id(param_1,0x17,register0x00000401);
  uVar1 = 1;
LAB_ram_0200954c:
  winSetVisible(uVar3,uVar1);
  return;
}

