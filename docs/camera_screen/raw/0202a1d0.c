/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202a1d0 Name: ui_close_all_windows
 */

/* Track2 evidence-based naming (likely, names_08.tsv): Walks the window sibling chain (next pointer
   at +0x28) from the root window DAT_ram_020ce0d4 calling winDestroy on each, then flags a
   widget-pool leak when freeBlks != maxBlks or minFreeBlks == 0 (printing the memPool error text
   and dumping pool stats) and returns 1 if it ran.  [was FUN_ram_0202a1d0] */

undefined4 ui_close_all_windows(void)

{
  undefined4 uVar1;
  undefined *p0;
  
  uVar1 = 0;
  p0 = DAT_ram_020ce0d4;
  if (DAT_ram_020ce0d0 != 0) {
    while (DAT_ram_020ce0d0 != 0) {
      if (*(int *)(p0 + 0x28) == 0) {
        winDestroy(p0);
        break;
      }
      winDestroy(p0);
      p0 = *(undefined **)(p0 + 0x28);
    }
    if ((DAT_ram_020ce0c0 != DAT_ram_020ce0c4) || (DAT_ram_020ce0c8 == 0)) {
      DAT_ram_020ce0dd = 1;
    }
    if (DAT_ram_020ce0dd != 0) {
      FUN_ram_02028ab8(s_mempool_err_ram_0208069c,register0x00000401);
      uart_Printf(s_memPool_error_information__ram_020806a8);
      ui_widget_pool_dump(&DAT_ram_020ce0b8);
    }
    uVar1 = 1;
    DAT_ram_020ce0d0 = 0;
  }
  return uVar1;
}

