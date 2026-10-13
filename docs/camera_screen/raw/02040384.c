/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02040384 Name: hal_mjpegEncodeInit
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: void hal_mjpegEncodeInit(void)   [hal/inc/hal_mjpeg.h] */

void hal_mjpegEncodeInit(void)

{
  char cVar1;
  uint uVar2;
  
  cVar1 = DAT_ram_020cf634;
  uVar2 = (uint)DAT_ram_020cf634;
  if (uVar2 == 0) {
    DAT_ram_020cf634 = '\x01';
    DAT_ram_020d41f8 = cVar1;
    _DAT_ram_020d4210 = uVar2;
    _DAT_ram_020d4214 = uVar2;
    _DAT_ram_020d4228 = uVar2;
    _DAT_ram_020d422c = uVar2;
    _DAT_ram_020d4238 = uVar2;
    hal_mjpegSrcCSI(&stack0x00000000);
    isp_init();
  }
  return;
}

