/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02044ce0 Name: hal_dispframeVideoCfg
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void hal_dispframeVideoCfg(disp_frame_t* p_frame,u16 posX,u16 posY,u16 width,u16
   height)   [hal/inc/hal_lcdframe.h] */

void hal_dispframeVideoCfg(undefined *p0,ushort p1,ushort p2,ushort p3,ushort p4)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  uint uVar4;
  uint uVar5;
  uint uVar6;
  uint uVar7;
  uint uVar8;
  uint uVar9;
  ushort local_38;
  ushort local_36;
  int local_34;
  int local_30;
  
  uVar5 = (uint)p1;
  uVar2 = Spr00000018;
  uVar9 = Spr00000018;
  uVar8 = (uint)p2;
  Spr00000018 = uVar9 & 0xfffffffd;
  uVar9 = (uint)p3;
  uVar1 = (uint)p4;
  if (p0[0x2e] == 0) {
    if ((((*(ushort *)(p0 + 0x24) != uVar5) || (*(ushort *)(p0 + 0x26) != uVar8)) ||
        (*(ushort *)(p0 + 0x20) != uVar9)) || (*(ushort *)(p0 + 0x22) != uVar1)) {
      uVar5 = uVar9 + 0x1f & 0xffffffe0;
      *(ushort *)(p0 + 0x20) = p3;
      iVar3 = uVar5 * uVar1;
      *(ushort *)(p0 + 0x22) = p4;
      *(int *)(p0 + 8) = *(int *)(p0 + 4) + iVar3;
      *(ushort *)(p0 + 0x24) = p1;
      *(ushort *)(p0 + 0x26) = p2;
      *(short *)(p0 + 0x28) = (short)uVar5;
      *(uint *)(p0 + 0x18) = (uint)(iVar3 * 3) >> 1;
    }
    goto LAB_ram_02044f20;
  }
  if (p0[0x2e] != 1) goto LAB_ram_02044f20;
  local_38 = 0;
  local_36 = 0;
  uVar6 = hal_lcdGetShowingRotate();
  uVar6 = uVar6 & 0xff;
  display_get_mode(&local_38,&local_36);
  uVar4 = uVar8;
  uVar7 = uVar5;
  if (uVar6 != 0) {
    if (uVar6 == 4) {
      uVar4 = (local_38 - uVar5) - uVar9;
    }
    else {
      if (uVar6 == 5) {
        uVar7 = (local_36 - uVar8) - uVar1 & 0xffff;
        uVar4 = uVar5;
        goto LAB_ram_02044e48;
      }
      uVar4 = (local_36 - uVar8) - uVar1;
      uVar8 = (local_38 - uVar5) - uVar9 & 0xffff;
    }
    uVar4 = uVar4 & 0xffff;
    uVar7 = uVar8;
  }
LAB_ram_02044e48:
  uVar5 = uVar1;
  if (uVar6 - 4 < 2) {
    uVar5 = uVar9;
    uVar9 = uVar1;
    if (uVar6 == 4) {
      uVar6 = 0;
    }
    else {
      uVar6 = 3;
    }
  }
  if (((*(ushort *)(p0 + 0x24) != uVar7) || (*(ushort *)(p0 + 0x26) != uVar4)) ||
     ((*(ushort *)(p0 + 0x20) != uVar9 || (*(ushort *)(p0 + 0x22) != uVar5)))) {
    uVar8 = uVar9 + 0x1f & 0xffffffe0;
    hal_lcdAddrCalculate(uVar9,uVar5,uVar6,uVar8,&local_34,&local_30);
    iVar3 = *(int *)(p0 + 4) + uVar8 * uVar5;
    *(int *)(p0 + 8) = iVar3;
    *(int *)(p0 + 0xc) = *(int *)(p0 + 4) + local_34;
    *(int *)(p0 + 0x10) = iVar3 + local_30;
    *(short *)(p0 + 0x20) = (short)uVar9;
    *(short *)(p0 + 0x22) = (short)uVar5;
    *(short *)(p0 + 0x24) = (short)uVar7;
    *(short *)(p0 + 0x26) = (short)uVar4;
    *(uint *)(p0 + 0x18) = uVar8 * uVar5 * 3 >> 1;
    *(short *)(p0 + 0x28) = (short)uVar8;
    p0[0x2a] = (sbyte)uVar6;
  }
LAB_ram_02044f20:
  uVar5 = Spr00000018;
  Spr00000018 = uVar2 & 2 | uVar5;
  return;
}

