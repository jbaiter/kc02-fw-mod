/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020071c4 Name: audio_playback_nv
 */

/* Track2 evidence-based naming (likely, names_01.tsv): Reads an NVM audio clip, sets the DAC sample
   rate/volume (16 kHz path) and starts playback with a completion callback.  [was FUN_ram_020071c4]
    */

uint audio_playback_nv(int param_1,sbyte param_2,sbyte param_3)

{
  undefined *p0;
  uint p0_00;
  int *piVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  int iVar5;
  
  iVar3 = nv_open(param_1);
  if (iVar3 < 0) {
    uVar2 = 1;
  }
  else {
    DAT_ram_020ccbc8 = nv_size(param_1);
    if (DAT_ram_020ccbc8 < 0x31) {
      uVar2 = 2;
    }
    else {
      uVar2 = DAT_ram_020ccbc8;
      if (DAT_ram_020ccd90 == 0) {
        DAT_ram_020ccbc8 = DAT_ram_020ccbc8 - 0x30 & 0xffffffe0;
        if (0x4afff < DAT_ram_020ccbc8) {
          DAT_ram_020ccbc8 = 0x4b000;
        }
        DAT_ram_020ccbcc = 0;
        if (DAT_ram_020ccbd0 == (undefined *)0x0) {
          DAT_ram_020ccbd0 = hal_sysMemMalloc(DAT_ram_020ccbc8 + 0x2000,0x40);
          if (DAT_ram_020ccbd0 == (undefined *)0x0) {
            return 3;
          }
          if (param_2 != 0) {
            iVar4 = XOSTimeGet();
            do {
              sys_irq_clear();
              iVar5 = audioPlaybackGetStatus();
              if (iVar5 != 1) break;
              iVar5 = XOSTimeGet();
            } while ((uint)(iVar5 - iVar4) < 0x3e9);
          }
          p0 = DAT_ram_020ccbd0;
          memset(DAT_ram_020ccbd0,0,DAT_ram_020ccbc8 + 0x2000,register0x00000401);
          ax32xx_sysDcacheFlush((uint)p0,DAT_ram_020ccbc8 + 0x2000);
          nv_read(iVar3 + 0x30,DAT_ram_020ccbd0,DAT_ram_020ccbc8);
          FUN_ram_020524c0(1);
          uVar2 = (uint)app_context.b6b;
          piVar1 = &DAT_ram_0207b9ac;
          p0_00 = 0;
          do {
            if (*piVar1 == 16000) {
              ax32xx_dacSampleRateSet(p0_00);
              uVar2 = uVar2 * 10;
              if (100 < uVar2) {
                uVar2 = 100;
              }
              ax32xx_dacVolumeSet((ushort)((uVar2 * 0x7fff) / 100));
              hal_dacCallBackRegister(&LAB_ram_02006e28);
              ax32xx_dacStart((uint)DAT_ram_020ccbd0,0x200);
              break;
            }
            piVar1 = piVar1 + 1;
            p0_00 = p0_00 + 1 & 0xffff;
          } while (p0_00 != 0xb);
          if (param_3 != 0) {
            do {
              sys_irq_clear();
              iVar3 = audioPlaybackGetStatus();
            } while (iVar3 == 1);
          }
        }
        uVar2 = 0;
      }
    }
  }
  return uVar2;
}

