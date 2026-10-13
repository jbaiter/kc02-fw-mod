/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02022060 Name: blit_b
 */

void blit_b(int param_1,uint param_2,uint param_3,ushort param_4,ushort param_5,uint param_6,
           ushort param_7,int param_8,ushort param_9,ushort param_10,sbyte param_11,sbyte param_12)

{
  sbyte sVar1;
  int iVar2;
  int iVar3;
  uint uVar4;
  uint uVar5;
  uint uVar6;
  uint uVar7;
  uint uVar8;
  int iVar9;
  
  iVar2 = (int)(short)(param_4 & 0xfffe);
  param_2 = param_2 & 0xffff;
  param_3 = param_3 & 0xffff;
  uVar7 = (uint)param_9;
  uVar5 = (uint)param_10;
  iVar3 = (int)(short)(param_5 & 0xfffe);
  if ((((0 < (int)(iVar2 + (param_6 & 0xfffe))) && (0 < (int)(iVar3 + (param_7 & 0xfffe)))) &&
      (iVar2 < (int)param_2)) && (iVar3 < (int)param_3)) {
    uVar4 = 0;
    if (iVar2 < 0) {
      uVar4 = -iVar2 & 0xffff;
      iVar2 = 0;
    }
    uVar6 = 0;
    if (iVar3 < 0) {
      uVar6 = -iVar3 & 0xffff;
      iVar3 = 0;
    }
    uVar8 = param_2 - iVar2;
    if ((int)(iVar2 + uVar7) <= (int)param_2) {
      uVar8 = uVar7 - uVar4;
    }
    uVar6 = uVar5 - uVar6;
    if ((int)param_3 < (int)(iVar3 + uVar5)) {
      uVar6 = param_3 - iVar3;
    }
    iVar9 = param_1 + param_2 * iVar3 + iVar2;
    param_1 = param_1 + iVar2 + param_2 * param_3 + ((int)(param_2 * iVar3) >> 1);
    iVar2 = param_8 + uVar4 + uVar7 * uVar5;
    param_8 = param_8 + uVar4;
    for (uVar5 = 0; uVar5 < (uVar6 & 0xffff); uVar5 = uVar5 + 2 & 0xffff) {
      for (uVar4 = 0; (uVar4 & 0xffff) < (uVar8 & 0xffff); uVar4 = uVar4 + 1) {
        if ((*(sbyte *)(param_8 + uVar4) != param_11) || (*(sbyte *)(iVar2 + uVar4) != param_12)) {
          *(sbyte *)(iVar9 + uVar4) = *(sbyte *)(param_8 + uVar4);
          *(sbyte *)(param_1 + uVar4) = *(sbyte *)(iVar2 + uVar4);
        }
      }
      for (uVar4 = 0; (uVar4 & 0xffff) < (uVar8 & 0xffff); uVar4 = uVar4 + 1) {
        sVar1 = *(sbyte *)(param_8 + uVar7 + uVar4);
        if ((sVar1 != param_11) || (*(sbyte *)(iVar2 + uVar4) != param_12)) {
          *(sbyte *)(iVar9 + uVar4 + param_2) = sVar1;
          *(sbyte *)(param_1 + uVar4) = *(sbyte *)(iVar2 + uVar4);
        }
      }
      iVar9 = iVar9 + param_2 * 2;
      param_1 = param_1 + param_2;
      iVar2 = iVar2 + uVar7;
      param_8 = param_8 + uVar7 * 2;
    }
  }
  return;
}

