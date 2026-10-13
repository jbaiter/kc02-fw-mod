/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203e5c4 Name: hal_sysMemMalloc
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void *hal_sysMemMalloc(u32 size,u8 algin)   [hal/inc/hal_sys.h] */

undefined * hal_sysMemMalloc(uint p0,byte p1)

{
  int *piVar1;
  uint uVar2;
  undefined3 in_register_00001011;
  uint uVar3;
  undefined4 *puVar4;
  undefined *puVar5;
  
  puVar4 = (undefined4 *)hal_memNodeMalloc(CONCAT31(in_register_00001011,p1),&stack0x00000000);
  if (puVar4 != (undefined4 *)0x0) {
    uVar2 = p0 + 0x3f & 0xffffffc0;
    for (piVar1 = DAT_ram_020cec54; piVar1 != (int *)0x0; piVar1 = (int *)piVar1[3]) {
      if ((*piVar1 == 0) && (uVar3 = piVar1[2], uVar2 <= uVar3)) {
        puVar5 = (undefined *)piVar1[1];
        *puVar4 = 0;
        puVar4[2] = uVar3 - uVar2;
        puVar4[1] = puVar5 + uVar2;
        puVar4[3] = piVar1[3];
        piVar1[3] = (int)puVar4;
        piVar1[2] = uVar2;
        *piVar1 = 1;
        return puVar5;
      }
    }
    *puVar4 = 0;
    puVar4[3] = DAT_ram_020cec58;
    DAT_ram_020cec58 = puVar4;
    hal_sysMemPrint();
  }
  return (undefined *)0x0;
}

