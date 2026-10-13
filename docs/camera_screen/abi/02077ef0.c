/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02077ef0 Name: FUN_ram_02077ef0
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_ram_02077ef0(uint param_1,ushort param_2,uint param_3,sbyte param_4,sbyte param_5)

{
  sbyte sVar1;
  uint p0;
  byte bVar2;
  sbyte *psVar3;
  undefined *p0_00;
  sbyte *psVar4;
  uint uVar5;
  uint uVar6;
  int iVar7;
  uint uVar8;
  ushort uStack_30;
  ushort uStack_2e;
  
  uVar6 = (uint)param_2;
  p0_00 = hal_rtcTimeGet();
  psVar4 = (sbyte *)hal_rtcTime2String(p0_00);
  hal_csiResolutionGet(&uStack_30,&uStack_2e);
  if (param_4 == 0) {
    if (param_5 != 0) {
      uStack_30 = 0x140;
      uStack_2e = 0xb4;
    }
  }
  else {
    uStack_30 = 0x3c0;
    uStack_2e = 0x2d0;
  }
  psVar3 = psVar4 + 0x13;
  uart_Printf(s_pos_x__d_pos_y__d_ram_020c20e8,param_1 & 0xffff,uVar6);
  uart_Printf(s_in_wm_dram__ram_020c20fb);
  iVar7 = 0;
  do {
    uart_Printf((undefined *)0x20c2107,(int)*psVar4,uVar6);
    p0 = _DAT_ram_020d4210;
    sVar1 = *psVar4;
    uVar8 = (uint)uStack_30;
    uVar5 = (uint)uStack_2e;
    bVar2 = 0;
    if ((((((sVar1 != 0x30) && (bVar2 = 1, sVar1 != 0x31)) && (bVar2 = 2, sVar1 != 0x32)) &&
         ((bVar2 = 3, sVar1 != 0x33 && (bVar2 = 4, sVar1 != 0x34)))) &&
        ((bVar2 = 5, sVar1 != 0x35 && ((bVar2 = 6, sVar1 != 0x36 && (bVar2 = 7, sVar1 != 0x37))))))
       && ((bVar2 = 8, sVar1 != 0x38 && ((bVar2 = 9, sVar1 != 0x39 && (bVar2 = 10, sVar1 != 0x2f))))
          )) {
      if (sVar1 == 0x3a) {
        bVar2 = 0xb;
      }
      else {
        bVar2 = app_context.b6f;
        if (sVar1 == 0x20) {
          bVar2 = 0xc;
        }
      }
    }
    app_context.b6f = bVar2;
    uVar6 = *(uint *)((uint)app_context.b6f * 4 + 0x20d68ec);
    blit_b(_DAT_ram_020d4210,uVar8,uVar5,(int)(((param_1 & 0xffff) + iVar7) * 0x10000) >> 0x10,
           (int)(short)param_2,uRam020d4102,uRam020d4104,uRam020d4102,0x7f,0x81);
    psVar4 = psVar4 + 1;
    ax32xx_sysDcacheWback(p0,(int)(uVar8 * uVar5 * 3) >> 1);
    iVar7 = iVar7 + (uint)uRam020d4100 + (param_3 & 0xffff);
  } while (psVar4 != psVar3);
  return;
}

