/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0205b0fc Name: kc02_print_image
 */

/* print from in-RAM JPEG buffer (entry of the print API) */

int kc02_print_image(undefined4 param_1,undefined1 param_2,sbyte param_3,undefined1 param_4,
                    undefined4 param_5)

{
  int iVar1;
  undefined *unaff_r14;
  undefined2 uStack_24;
  undefined2 uStack_22;
  undefined *puStack_20;
  
  kc02_printer_init();
  if (DAT_ram_020ccdc8 != 0) {
    sRam020c8794 = 0;
  }
  if (sRam020c8794 == 0) {
    iVar1 = kc02_load_print_image(param_1,&puStack_20,&uStack_24,&uStack_22,param_5);
    unaff_r14 = puStack_20;
    if (iVar1 == 0) {
      if (param_3 == 1) {
        kc02_print_layout(puStack_20,uStack_24,uStack_22);
      }
      kc02_sfr4800_save_restore(0);
      if (param_3 == 1) {
        iVar1 = kc02_print_run_B();
      }
      else {
        iVar1 = kc02_print_run_A(puStack_20,uStack_24,uStack_22,param_2,param_3,param_4);
      }
      kc02_sfr4800_save_restore(1);
    }
  }
  else {
    iVar1 = 1;
    win_open(0x20c4434,2,0x810000a4,5);
  }
  if (unaff_r14 != (undefined *)0x0) {
    hal_sysMemFree(unaff_r14);
  }
  hal_gpioInit(0,0x80,0,0);
  hal_gpioWrite(0,0x80,1);
  ui_state_apply(0);
  return iVar1;
}

