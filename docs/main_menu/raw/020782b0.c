/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020782b0 Name: memcpy
 */

/* SDK align: lib_a-memcpy.o/memcpy score=1.0 segment */

undefined4 * memcpy(undefined4 *param_1,undefined4 *param_2,uint param_3)

{
  undefined4 *puVar1;
  undefined4 *puVar2;
  undefined4 *puVar3;
  undefined4 *puVar4;
  int iVar5;
  uint uVar6;
  undefined4 *puVar7;
  undefined4 *puVar8;
  undefined4 *puVar9;
  undefined4 *puVar10;
  undefined4 *puVar11;
  uint uVar12;
  
  uVar12 = param_3;
  puVar2 = param_1;
  if (0xf < param_3) {
    if ((((uint)param_1 | (uint)param_2) & 3) != 0) goto LAB_ram_020783b8;
    uVar12 = param_3 - 0x10 >> 4;
    puVar11 = param_1 + 1;
    puVar10 = param_2 + 2;
    puVar9 = param_1 + 2;
    puVar8 = param_2 + 3;
    puVar7 = param_1 + 3;
    puVar2 = param_2 + 1;
    puVar3 = param_1;
    puVar4 = param_2;
    do {
      puVar1 = puVar2 + 4;
      *puVar3 = *puVar4;
      puVar4 = puVar4 + 4;
      *puVar11 = *puVar2;
      puVar3 = puVar3 + 4;
      puVar11 = puVar11 + 4;
      *puVar9 = *puVar10;
      puVar10 = puVar10 + 4;
      puVar9 = puVar9 + 4;
      *puVar7 = *puVar8;
      puVar8 = puVar8 + 4;
      puVar7 = puVar7 + 4;
      puVar2 = puVar1;
    } while (puVar1 != param_2 + uVar12 * 4 + 5);
    iVar5 = uVar12 + 1;
    uVar12 = param_3 & 0xf;
    puVar2 = param_1 + iVar5 * 4;
    param_2 = param_2 + iVar5 * 4;
    uVar6 = uVar12;
    puVar3 = param_2;
    puVar4 = puVar2;
    if (3 < uVar12) {
      do {
        uVar6 = uVar6 - 4;
        *puVar4 = *puVar3;
        puVar3 = puVar3 + 1;
        puVar4 = puVar4 + 1;
      } while (3 < uVar6);
      iVar5 = (uVar12 - 4 & 0xfffffffc) + 4;
      param_2 = (undefined4 *)((int)param_2 + iVar5);
      uVar12 = param_3 & 3;
      puVar2 = (undefined4 *)((int)puVar2 + iVar5);
    }
  }
  param_3 = uVar12;
  if (param_3 == 0) {
    return param_1;
  }
LAB_ram_020783b8:
  puVar3 = puVar2;
  do {
    puVar4 = (undefined4 *)((int)puVar3 + 1);
    *(undefined1 *)puVar3 = *(undefined1 *)param_2;
    param_2 = (undefined4 *)((int)param_2 + 1);
    puVar3 = puVar4;
  } while (puVar4 != (undefined4 *)((int)puVar2 + param_3));
  return param_1;
}

