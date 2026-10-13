/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020049f8 Name: mjpeg_start_recording
 */

/* Track2 evidence-based naming (likely, names_01.tsv): Computes the CSI/LCD crop and calls
   hal_mjpegAVIStart for 720p/VGA AVI recording, guarded by the record-active flags
   DAT_ram_020c8a01/UNK_ram_020c8a00.  [was FUN_ram_020049f8] */

undefined4 mjpeg_start_recording(uint param_1,sbyte param_2,ushort param_3,ushort param_4)

{
  ushort unaff_r2;
  int iVar1;
  byte bVar2;
  uint uVar3;
  byte bVar4;
  undefined1 *puVar5;
  ushort unaff_r18;
  uint uVar6;
  uint uVar7;
  ushort local_1c;
  ushort local_1a;
  
  param_1 = param_1 & 0xff;
  hal_csiResolutionGet(&local_1c,&local_1a);
  uVar3 = (uint)local_1c;
  if (uVar3 == 0x500) {
    if (param_1 < 0x11) {
      iVar1 = (int)((uint)app_context.b8 * 5) / -3 + 100;
      uVar3 = (iVar1 * 0x2d0) / 100 & 0xfffe;
      uVar6 = (iVar1 * 0x500) / 100 + 0x1fU & 0xffe0;
      param_4 = (ushort)((int)(local_1a - uVar3) / 2);
      param_3 = (ushort)((int)(0x500 - uVar6) / 2);
      unaff_r2 = param_4 + (short)uVar3;
      unaff_r18 = param_3 + (short)uVar6;
    }
    hal_lcdSetCsiCrop(param_3,param_4,unaff_r18,unaff_r2);
    if (param_2 == 0) {
      puVar5 = &DAT_ram_020c8a01;
      if (DAT_ram_020c8a01 != 0) {
        return 0;
      }
      bVar2 = 0;
      bVar4 = 0xd0;
LAB_ram_02004c98:
      hal_mjpegAVIStart(local_1c,local_1a,bVar2,bVar4);
      *puVar5 = 1;
      return 0;
    }
    bVar2 = 0;
    bVar4 = 0xd0;
  }
  else {
    if (param_1 < 0x11) {
      uVar7 = (*(int *)(&DAT_ram_0207b8c4 + param_1 * 4) * uVar3) / 100 & 0xfffe;
      uVar6 = (*(int *)(&DAT_ram_0207b8c4 + param_1 * 4) * (uint)local_1a) / 100 & 0xfffe;
      param_3 = (ushort)((int)(uVar3 - uVar7) / 2);
      param_4 = (ushort)((int)(local_1a - uVar6) / 2);
      unaff_r18 = param_3 + (short)uVar7;
      unaff_r2 = param_4 + (short)uVar6;
    }
    hal_lcdSetCsiCrop(param_3,param_4,unaff_r18,unaff_r2);
    if (param_2 == 0) {
      puVar5 = &UNK_ram_020c8a00;
      if (UNK_ram_020c8a00 != 0) {
        return 0;
      }
      bVar2 = 0x80;
      bVar4 = 0x68;
      goto LAB_ram_02004c98;
    }
    bVar2 = 0x80;
    bVar4 = 0x68;
  }
  hal_mjpegAVIStart(local_1c,local_1a,bVar2,bVar4);
  return 0;
}

