/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02077cf4 Name: FUN_ram_02077cf4
 */

void FUN_ram_02077cf4(ushort param_1,ushort param_2,char param_3)

{
  uint p0;
  uint uVar1;
  uint uVar2;
  uint uVar3;
  int iVar4;
  char *pcVar5;
  undefined *puVar6;
  uint uVar7;
  int *piVar8;
  
  cRam020d40fc = param_3;
  uRam020d40fe = param_2;
  uRam020d4100 = param_1;
  uart_Printf(s_in_lib___ram_020c2070,&stack0x00000000);
  uVar1 = (uint)uRam020d4100;
  uVar2 = (uint)uRam020d40fe;
  uVar3 = (uint)cRam020d40fc;
  uart_Printf(s_wm_bmp2yuv_init_w__d_h__d_num__d_ram_020c207a,uVar1,uVar2,uVar3);
  uRam020d4104 = uRam020d4100 + 1 & 0xfffe;
  uRam020d4102 = uRam020d40fe + 1 & 0xfffe;
  for (uVar7 = 0; p0 = (uint)uRam020d4104 * (uint)uRam020d4102 * 3, uVar7 < cRam020d40fc;
      uVar7 = uVar7 + 1 & 0xff) {
    puVar6 = hal_sysMemMalloc((int)p0 >> 1,0x20);
    *(undefined **)(uVar7 * 4 + 0x20d68ec) = puVar6;
  }
  uVar7 = 0;
  puVar6 = hal_sysMemMalloc(p0,0x20);
  if (puVar6 == (undefined *)0x0) {
LAB_ram_02077e08:
    uart_Printf(s_mem_err__ram_020c209c,uVar1,uVar2,uVar3);
  }
  else {
    for (; uVar7 < cRam020d40fc; uVar7 = uVar7 + 1 & 0xff) {
      piVar8 = (int *)(uVar7 * 4 + 0x20d68ec);
      iVar4 = *piVar8;
      if (iVar4 == 0) goto LAB_ram_02077e08;
      icon_decode(*(undefined4 *)(uVar7 * 4 + 0x20c2ae8),puVar6,
                  iVar4 + (uint)uRam020d4104 * (uint)uRam020d4102);
      pcVar5 = (char *)*piVar8;
      uVar2 = (uint)*pcVar5;
      uVar3 = (uint)pcVar5[(uint)uRam020d4104 * (uint)uRam020d4102];
      uVar1 = uVar7;
      uart_Printf(s_id__d_first_pixel_y_0x_x_u_0x_x__ram_020c20a6,uVar7,uVar2,uVar3,(uint)pcVar5[1],
                  (uint)(pcVar5 + (uint)uRam020d4104 * (uint)uRam020d4102)[1]);
    }
    uart_Printf(s_free_rgb24_buf_ts_ram_020c20d5,uVar1,uVar2,uVar3);
    hal_sysMemFree(puVar6);
  }
  return;
}

