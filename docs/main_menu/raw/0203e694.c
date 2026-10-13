/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203e694 Name: hal_sysMemFree
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void hal_sysMemFree(void *mem)   [hal/inc/hal_sys.h] */

void hal_sysMemFree(undefined *p0)

{
  int *piVar1;
  int *piVar2;
  int *piVar3;
  
  if (p0 != (undefined *)0x0) {
    piVar1 = DAT_ram_020cec54;
    piVar3 = (int *)0x0;
    while (piVar2 = piVar1, piVar2 != (int *)0x0) {
      if ((*piVar2 != 0) && ((undefined *)piVar2[1] == p0)) {
        piVar1 = (int *)piVar2[3];
        *piVar2 = 0;
        if ((piVar1 != (int *)0x0) && (*piVar1 == 0)) {
          piVar2[2] = piVar2[2] + piVar1[2];
          piVar2[3] = piVar1[3];
          *piVar1 = 0;
          piVar1[3] = (int)DAT_ram_020cec58;
          DAT_ram_020cec58 = piVar1;
        }
        if (piVar3 == (int *)0x0) {
          return;
        }
        if (*piVar3 != 0) {
          return;
        }
        piVar3[2] = piVar3[2] + piVar2[2];
        piVar3[3] = piVar2[3];
        *piVar2 = 0;
        piVar2[3] = (int)DAT_ram_020cec58;
        DAT_ram_020cec58 = piVar2;
        return;
      }
      piVar3 = piVar2;
      piVar1 = (int *)piVar2[3];
    }
  }
  return;
}

