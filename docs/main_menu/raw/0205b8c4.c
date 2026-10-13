/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0205b8c4 Name: video_present
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* LCD present / buffer blit */

void video_present(sbyte param_1)

{
  uint p0;
  int iVar1;
  undefined *p0_00;
  int local_18;
  
  boardIoctrl((int)(sbyte)app_context.b13,0x20000013,&local_18);
  if (local_18 == 0) {
    p0 = FUN_ram_02004840(1);
    memset(p0,0xfa,(uint)_DAT_ram_020d2ef6 * (uint)_DAT_ram_020d2ef4);
    ax32xx_sysDcacheWback(p0,(uint)_DAT_ram_020d2ef6 * (uint)_DAT_ram_020d2ef4);
    FUN_ram_020048f8(1,p0);
    ax32xx_deUpdate();
  }
  else {
    iVar1 = ui_close_all_windows();
    if (iVar1 == 0) {
      if (((param_1 == 0) && (app_context.b66 == 0)) && (app_context.b68 == 0)) {
        return;
      }
      app_context.b66 = 0;
    }
    ui_invoke_key_13();
    if (DAT_ram_020d2ef8 != 0) {
      p0_00 = (undefined *)FUN_ram_02004840(1);
      ax32xx_sys_dma_memkick
                (p0_00,_DAT_ram_020d2efc,(uint)_DAT_ram_020d2ef6 * (uint)_DAT_ram_020d2ef4);
      if (app_context.b68 != 0) {
        app_context.b68 = 0;
        app_context.b69 = 0;
        app_context.b66 = 1;
      }
      FUN_ram_020048f8(1,p0_00);
    }
    ax32xx_deUpdate();
    if (app_context.b68 != 0) {
      XOSTimeDly(0x1e);
    }
  }
  return;
}

