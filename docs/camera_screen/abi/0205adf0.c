/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0205adf0 Name: kc02_print_file
 */

/* print from file fd (12-byte JRX record header) */

int kc02_print_file(int param_1,undefined1 param_2,sbyte param_3,undefined1 param_4)

{
  int iVar1;
  uint uVar2;
  uint uVar3;
  undefined *p1;
  int iVar4;
  undefined2 uStack_38;
  undefined2 uStack_36;
  ushort uStack_34;
  ushort uStack_32;
  undefined *puStack_30;
  sbyte sStack_2c;
  sbyte sStack_2b;
  sbyte sStack_2a;
  char cStack_28;
  char cStack_27;
  char cStack_26;
  char cStack_25;
  
  kc02_printer_init();
  iVar4 = 2;
  iVar1 = fs_size(param_1);
  if (iVar1 < 0x200) goto LAB_ram_0205b0a0;
  if (DAT_ram_020ccdc8 != 0) {
    sRam020c8794 = 0;
  }
  if (sRam020c8794 == 0) {
    uVar2 = fs_size(param_1);
    uVar3 = hal_sysMemRemain();
    if (uVar3 + 0x40 < uVar2) {
      p1 = hal_sysMemMalloc(0x7d000,0x40);
      if (p1 != (undefined *)0x0) {
        uVar2 = fs_size(param_1);
        read(param_1,p1,0x7d000);
        uVar3 = 0x7d000;
LAB_ram_0205af44:
        ax32xx_sysDcacheFlush((uint)p1,uVar3);
        mjpeg_parse_image_header(p1);
        hal_mjpegDecodeGetResolution(&uStack_34,&uStack_32);
        if (0x9ff < uStack_34) {
          lseek(param_1,uVar2 - 0xc,0);
          read(param_1,(undefined *)&puStack_30,0xc);
          if (((sStack_2c == 0x4a) && (sStack_2b == 0x52)) && (sStack_2a == 0x58)) {
            lseek(param_1,(uint)cStack_28 << 0x18 | (uint)cStack_27 << 0x10 | (uint)cStack_25 |
                          (uint)cStack_26 << 8,0);
            read(param_1,p1,0x7d000);
            ax32xx_sysDcacheWback((uint)p1,0x7d000);
          }
        }
        iVar4 = kc02_load_print_image(p1,&puStack_30,&uStack_38,&uStack_36,uVar2);
        if (iVar4 == 0) {
          if (param_3 == 1) {
            kc02_print_layout(puStack_30,uStack_38,uStack_36);
          }
          kc02_sfr4800_save_restore(0);
          if (param_3 == 1) {
            iVar4 = kc02_print_run_B();
          }
          else {
            iVar4 = kc02_print_run_A(puStack_30,uStack_38,uStack_36,param_2,param_3,param_4);
          }
          kc02_sfr4800_save_restore(1);
        }
        if (puStack_30 != (undefined *)0x0) {
          hal_sysMemFree(puStack_30);
        }
        hal_sysMemFree(p1);
        goto LAB_ram_0205b0a0;
      }
    }
    else {
      uVar2 = fs_size(param_1);
      p1 = hal_sysMemMalloc(uVar2,0x40);
      if (p1 != (undefined *)0x0) {
        uVar3 = fs_size(param_1);
        read(param_1,p1,uVar3);
        uVar2 = uVar3;
        goto LAB_ram_0205af44;
      }
    }
  }
  else {
    win_open(0x20c4434,2,0x810000a4,5);
  }
  iVar4 = 1;
LAB_ram_0205b0a0:
  hal_gpioInit(0,0x80,0,0);
  hal_gpioWrite(0,0x80,1);
  ui_state_apply(0);
  return iVar4;
}

