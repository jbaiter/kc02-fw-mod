/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02013138 Name: FUN_ram_02013138
 */

void FUN_ram_02013138(int param_1)

{
  undefined *p0;
  int iVar1;
  ushort uStack_14;
  ushort uStack_12;
  
  p0 = dispLayerGetFrame(0);
  if (p0 != (undefined *)0x0) {
    display_get_mode(&uStack_14,&uStack_12);
    hal_dispframeVideoCfg(p0,0,0,uStack_14,uStack_12);
    if (param_1 == 0) {
      memcpy(*(undefined4 *)(p0 + 4),uRam020ccecc,
             (int)((uint)uRam020cceca * (uint)uRam020ccec8 * 3) >> 1);
      iVar1 = (uint)app_context.b67 * 4;
      blit_b(*(undefined4 *)(p0 + 4),*(undefined2 *)(p0 + 0x20),*(undefined2 *)(p0 + 0x22),
             (int)*(short *)(&main_menu_icon_coords + iVar1),(int)*(short *)(iVar1 + 0x207bb62),
             uRam020cceac,*(undefined4 *)(iVar1 + 0x20cceb0),uRam020cceae,uRam020cceac,0x8a,0x81);
      ax32xx_sysDcacheFlush
                (*(uint *)(p0 + 4),
                 (int)((uint)*(ushort *)(p0 + 0x20) * (uint)*(ushort *)(p0 + 0x22) * 3) >> 1);
    }
    else {
      memcpy(*(undefined4 *)(p0 + 4),uRam020ccecc,
             (int)((uint)uRam020cceca * (uint)uRam020ccec8 * 3) >> 1);
      ax32xx_sysDcacheFlush
                (*(uint *)(p0 + 4),
                 (int)((uint)*(ushort *)(p0 + 0x20) * (uint)*(ushort *)(p0 + 0x22) * 3) >> 1);
    }
    dispLayerShow(p0,0,0,0xff);
  }
  return;
}

