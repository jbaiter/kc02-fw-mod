/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0205b49c Name: nv_size
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: int nv_size(int res_num)   [bwlib/nvfs/nvfs.h] */

int nv_size(int p0)

{
  int iVar1;
  
  iVar1 = -1;
  if ((_DAT_ram_020d2edc != 0) && (iVar1 = _DAT_ram_020d2ef0, _DAT_ram_020d2ee8 != p0)) {
    iVar1 = -1;
    if ((uint)(p0 * 8) < _DAT_ram_020d2ee4) {
      hal_spiFlashRead(p0 * 8 + _DAT_ram_020d2edc,0x20d2eec,8);
      iVar1 = _DAT_ram_020d2ef0;
      _DAT_ram_020d2ee8 = p0;
    }
  }
  return iVar1;
}

