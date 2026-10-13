/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02008188 Name: mode_manager_loop
 */

/* close->open->service of the selected task (curTask at 0x020CCD58) */

void mode_manager_loop(void)

{
  uint uVar1;
  int iVar2;
  int iVar3;
  
  iVar3 = 0;
  while (DAT_ram_020ccd50 != 0xb) {
    if (puRam020ccd58 != (undefined *)0x0) {
      if (*(code **)(puRam020ccd58 + 0xc) != (code *)0x0) {
        (**(code **)(puRam020ccd58 + 0xc))(*(undefined4 *)(puRam020ccd58 + 4),register0x00000401);
      }
      winDestroyDeskTopChildWin();
    }
    iVar2 = iVar3 + 1;
    uiHeapInfor(iVar3);
    uVar1 = DAT_ram_020ccd50;
    DAT_ram_020ccd54 = DAT_ram_020ccd50;
    DAT_ram_020ccd50 = 0xc;
    puRam020ccd58 = task_array[uVar1];
    if (puRam020ccd58 == (undefined *)0x0) {
      DAT_ram_020ccd54 = 1;
      puRam020ccd58 = task_array[1];
    }
    if (*(code **)(puRam020ccd58 + 8) != (code *)0x0) {
      (**(code **)(puRam020ccd58 + 8))(*(undefined4 *)(puRam020ccd58 + 4));
    }
    if (DAT_ram_020ccd54 != 1) {
      video_present(1);
    }
    while (iVar3 = iVar2, 0xb < DAT_ram_020ccd50) {
      app_main_loop();
      if (*(code **)(puRam020ccd58 + 0x10) != (code *)0x0) {
        (**(code **)(puRam020ccd58 + 0x10))(*(undefined4 *)(puRam020ccd58 + 4));
      }
    }
  }
  return;
}

