/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203ad2c Name: FUN_ram_0203ad2c
 */

undefined4 FUN_ram_0203ad2c(byte param_1,byte param_2,byte param_3,byte param_4)

{
  sbyte sVar1;
  ushort p0;
  ushort p1;
  int unaff_r2;
  int unaff_r14;
  uint p0_00;
  uint uVar2;
  uint p3;
  uint p1_00;
  
  ax32xx_videoSetScale(param_1,param_2,param_3,param_4);
  p0 = *(ushort *)(*(int *)(unaff_r14 + 0x18) + 0x20);
  p1 = *(ushort *)(*(int *)(unaff_r14 + 0x18) + 0x22);
  ax32xx_osdSetUpscaler(p0,p1,p0,p1,(uint *)0x0);
  ax32xx_videoEnable(1);
  ax32xx_deUpdate();
  if (*(sbyte *)(unaff_r2 + 0x20) == 1) {
    func_0x02035a6c(*(undefined1 *)(unaff_r2 + 0x21),
                    -((*(char *)(unaff_r2 + 0x27) ^ 0xffffffff) & 0xff) >> 0x1f);
    ax32xx_lcdInit();
    ax32xx_lcdIRQEnable(0x13,1);
    func_0x02035720(1);
    ax32xx_lcdPreLineSet(7);
    ax32xx_lcdSignalSet(0,0,1,0,0,1,0,0);
    ax32xx_lcdBusWidth((uint)*(char *)(unaff_r2 + 0x21));
    ax32xx_lcdClkSet(0x38);
    ax32xx_lcdSyncSet(0,0,0,0);
    ax32xx_lcdDESignalSet(0,0,0,0);
    ax32xx_lcdPositionSet(0,0);
    ax32xx_lcdResolutionSet((uint)*(ushort *)(unaff_r2 + 0x34),(uint)*(ushort *)(unaff_r2 + 0x36));
    ax32xx_lcdWindowSizeSet(*(ushort *)(unaff_r2 + 0x34) - 1,*(ushort *)(unaff_r2 + 0x36) - 1);
    ax32xx_lcdDataModeSet
              (*(uint *)(unaff_r2 + 0x2c),*(uint *)(unaff_r2 + 0x30),*(byte *)(unaff_r2 + 0x5d),
               *(byte *)(unaff_r2 + 0x5e));
    ax32xx_lcdClkNumberSet
              ((uint)*(ushort *)(unaff_r2 + 0x34) * (uint)*(ushort *)(unaff_r2 + 0x36) *
               (uint)*(char *)(unaff_r2 + 0x5c) - 1);
    ax32xx_lcdEndLineSet(0,0);
    ax32xx_lcdEnable(1);
    func_0x020394b8(unaff_r2 + 0x22,*(undefined4 *)(unaff_r2 + 0x4c));
    ax32xx_lcdClkSet((uint)*(char *)(unaff_r2 + 0x28));
    ax32xx_sysCpuMsDelay(1);
    sVar1 = *(sbyte *)(unaff_r2 + 0x27);
    if (sVar1 == 0) {
      ax32xx_lcdTeMode(0,1);
      ax32xx_lcdIRQEnable(0x14,1);
    }
    else if (sVar1 == 1) {
      ax32xx_lcdISRRegister(0x14,0x203948c);
      ax32xx_lcdIRQEnable(0x14,1);
      ax32xx_lcdTeMode(0,1);
    }
    else if (sVar1 == -1) {
      ax32xx_lcdKick();
    }
  }
  else {
    func_0x02035b50(*(undefined1 *)(unaff_r2 + 0x21),
                    -((*(char *)(unaff_r2 + 0x66) ^ 0xffffffff) & 0xff) >> 0x1f);
    hal_lcdParaRGB(unaff_r2,*(undefined4 *)(unaff_r2 + 0x4c));
    ax32xx_lcdInit();
    ax32xx_lcdIRQEnable(0x11,1);
    func_0x02035720(0);
    ax32xx_lcdPreLineSet(7);
    ax32xx_lcdSignalSet(*(byte *)(unaff_r2 + 0x60),*(char *)(unaff_r2 + 0x5f) & '\x01',
                        *(char *)(unaff_r2 + 0x66) & '\x01',*(char *)(unaff_r2 + 0x67) & '\x01',
                        *(byte *)(unaff_r2 + 0x68),
                        (byte)(-((*(char *)(unaff_r2 + 0x66) ^ 0xffffffff) & 0xff) >> 0x1f),
                        (byte)(-((*(char *)(unaff_r2 + 0x67) ^ 0xffffffff) & 0xff) >> 0x1f),
                        (byte)(-((*(byte *)(unaff_r2 + 0x68) ^ 0xffffffff) & 0xff) >> 0x1f));
    ax32xx_lcdBusWidth((uint)*(char *)(unaff_r2 + 0x21));
    if (*(int *)(unaff_r2 + 0x4c) != 0) {
      ax32xx_lcdBusEnable(1,0);
    }
    ax32xx_lcdClkSet((uint)*(char *)(unaff_r2 + 0x28));
    ax32xx_lcdSyncSet(0,(uint)*(char *)(unaff_r2 + 99),0,(uint)*(char *)(unaff_r2 + 0x60));
    p0_00 = (uint)*(char *)(unaff_r2 + 99) +
            (uint)*(char *)(unaff_r2 + 100) * (uint)*(char *)(unaff_r2 + 0x5c);
    uVar2 = (uint)*(char *)(unaff_r2 + 0x60) + (uint)*(char *)(unaff_r2 + 0x61);
    p1_00 = p0_00 + (uint)*(ushort *)(unaff_r2 + 0x34) * (uint)*(char *)(unaff_r2 + 0x5c);
    p3 = (*(ushort *)(unaff_r2 + 0x36) - 1) + uVar2;
    ax32xx_lcdDESignalSet(p0_00,p1_00,uVar2,p3);
    ax32xx_lcdPositionSet(p0_00,uVar2);
    ax32xx_lcdResolutionSet((uint)*(ushort *)(unaff_r2 + 0x34),(uint)*(ushort *)(unaff_r2 + 0x36));
    ax32xx_lcdWindowSizeSet
              (p1_00 + (uint)*(char *)(unaff_r2 + 0x65) * (uint)*(char *)(unaff_r2 + 0x5c),
               (uint)*(char *)(unaff_r2 + 0x62) + (uint)*(char *)(unaff_r2 + 0x60) + p3);
    ax32xx_lcdDataModeSet
              (*(uint *)(unaff_r2 + 0x2c),*(uint *)(unaff_r2 + 0x30),*(byte *)(unaff_r2 + 0x5d),
               *(byte *)(unaff_r2 + 0x5e));
    ax32xx_lcdClkNumberSet(0);
    uVar2 = uVar2 + *(ushort *)(unaff_r2 + 0x36);
    ax32xx_lcdEndLineSet(uVar2,uVar2 + 2);
    ax32xx_lcdEnable(1);
  }
  hal_lcdFrameEndCallBack(0x2039354);
  hal_rotateInit();
  ax32xx_csiISRRegiser(1,0x203a2f0);
  return 0;
}

