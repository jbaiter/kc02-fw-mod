/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02000438 Name: ui_invoke_key_13
 */

/* Track2 evidence-based naming (likely, names_01.tsv): UI callback that fetches the UI context with
   ui_get_ctx and raises key/action 0x13 via ui_key_invoke.  [was FUN_ram_02000438] */

void ui_invoke_key_13(void)

{
  undefined4 uVar1;
  
  uVar1 = ui_get_ctx(&stack0x00000000);
  ui_key_invoke(0x13,uVar1,0);
  return;
}

