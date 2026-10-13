/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02021d00 Name: icon_decode
 */

undefined4
icon_decode(int param_1,undefined *param_2,undefined1 *param_3,undefined1 *param_4,uint param_5,
           uint param_6)

{
  uint uVar1;
  undefined1 uVar2;
  uint uVar3;
  char *pcVar4;
  undefined1 *puVar5;
  undefined1 *puVar6;
  int iVar7;
  uint uVar8;
  undefined4 uVar9;
  int iVar10;
  char *pcVar11;
  uint uVar12;
  
  param_5 = param_5 & 0xffff;
  param_6 = param_6 & 0xffff;
  iVar7 = nv_open(param_1);
  uVar8 = nv_size(param_1);
  uVar9 = 1;
  if (0x35 < uVar8) {
    uVar1 = (param_5 + 1) * 3 & 0xfffffffc;
    uVar3 = uVar8 - 0x36;
    if (uVar1 * param_6 < uVar8 - 0x36) {
      uVar3 = uVar1 * param_6;
    }
    nv_read(iVar7 + 0x36,param_2,uVar3);
    pcVar4 = param_2 + uVar1 * (param_6 - 1);
    puVar5 = param_3;
    for (uVar8 = 0; puVar6 = puVar5, pcVar11 = pcVar4, uVar8 < param_6; uVar8 = uVar8 + 2) {
      while ((uint)((int)puVar6 - (int)puVar5) < param_5) {
        uVar12 = (uint)*(char *)(pcVar11[5] + 0x20cdd8c) +
                 (uint)*(char *)(pcVar11[4] + 0x20cde8c) + (uint)*(char *)(pcVar11[3] + 0x20cdf8c);
        iVar10 = (((uint)*(char *)(pcVar11[3] + 0x20cd98c) - (uint)*(char *)(pcVar11[4] + 0x20cd88c)
                  ) + 0x80) - (uint)*(char *)(pcVar11[5] + 0x20cd78c);
        iVar7 = (((uint)*(char *)(pcVar11[2] + 0x20cda8c) - (uint)*(char *)(*pcVar11 + 0x20cdc8c)) -
                (uint)*(char *)(pcVar11[1] + 0x20cdb8c)) + 0x80;
        uVar3 = (uint)*(char *)(pcVar11[2] + 0x20cdd8c) +
                (uint)*(char *)(pcVar11[1] + 0x20cde8c) + (uint)*(char *)(*pcVar11 + 0x20cdf8c);
        if (0xff < uVar3) {
          uVar3 = 0xff;
        }
        uVar2 = 0;
        *puVar6 = (sbyte)uVar3;
        if (-1 < iVar7) {
          if (0xff < iVar7) {
            iVar7 = 0xff;
          }
          uVar2 = (undefined1)iVar7;
        }
        *param_4 = uVar2;
        uVar2 = (undefined1)uVar12;
        if (0xff < uVar12) {
          uVar2 = 0xff;
        }
        puVar6[1] = uVar2;
        uVar2 = 0;
        if (-1 < iVar10) {
          if (0xff < iVar10) {
            iVar10 = 0xff;
          }
          uVar2 = (undefined1)iVar10;
        }
        param_4[1] = uVar2;
        puVar6 = puVar6 + 2;
        pcVar11 = pcVar11 + 6;
        param_4 = param_4 + 2;
      }
      puVar5 = puVar6;
      pcVar11 = pcVar4 + -uVar1;
      while ((uint)((int)puVar5 - (int)puVar6) < param_5) {
        uVar12 = (uint)*(char *)(pcVar11[5] + 0x20cdd8c) +
                 (uint)*(char *)(pcVar11[4] + 0x20cde8c) + (uint)*(char *)(pcVar11[3] + 0x20cdf8c);
        uVar3 = (uint)*(char *)(pcVar11[2] + 0x20cdd8c) +
                (uint)*(char *)(pcVar11[1] + 0x20cde8c) + (uint)*(char *)(*pcVar11 + 0x20cdf8c);
        uVar2 = (undefined1)uVar3;
        if (0xff < uVar3) {
          uVar2 = 0xff;
        }
        *puVar5 = uVar2;
        uVar2 = (undefined1)uVar12;
        if (0xff < uVar12) {
          uVar2 = 0xff;
        }
        puVar5[1] = uVar2;
        puVar5 = puVar5 + 2;
        pcVar11 = pcVar11 + 6;
      }
      pcVar4 = pcVar4 + uVar1 * -2;
    }
    ax32xx_sysDcacheFlush((uint)param_3,(int)(param_5 * param_6 * 3) >> 1);
    uVar9 = 0;
  }
  return uVar9;
}

