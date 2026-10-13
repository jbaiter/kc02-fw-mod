/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020416cc Name: mjpeg_get_frame
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* SDK hal_mjpeg/stream chunk get */

sbyte * mjpeg_get_frame(undefined4 *param_1,uint *param_2,int *param_3,int *param_4)

{
  sbyte *psVar1;
  int iVar2;
  sbyte *local_30;
  uint local_2c;
  int local_28;
  int local_24;
  
  *param_3 = 0;
  *param_4 = 0;
  iVar2 = hal_streamOut(&DAT_ram_020d4254,(uint *)&local_30,&local_2c,&local_28,&local_24);
  psVar1 = local_30;
  if (iVar2 < 0) {
    return (sbyte *)0x0;
  }
  if ((local_2c == 0) || ((DAT_ram_020d41f8 != 1 && (_DAT_ram_020d4248 == local_30)))) {
LAB_ram_02041740:
    hal_mjpegRawBufferfree();
    local_30 = (sbyte *)0x0;
  }
  else {
    ax32xx_sysDcacheFlush((uint)local_30,0x10);
    if (DAT_ram_020d41f8 == 1) {
      if (DAT_ram_020d4240 != 0) {
LAB_ram_020417a8:
        DAT_ram_020d4240 = 0;
        if (((*psVar1 != -1) || (psVar1[1] != -0x28)) && ((psVar1[8] != -1 || (psVar1[9] != -0x28)))
           ) goto LAB_ram_02041740;
      }
    }
    else if (DAT_ram_020d41f8 == 0) goto LAB_ram_020417a8;
    if (param_1 != (undefined4 *)0x0) {
      *param_1 = local_30;
    }
    if (param_2 != (uint *)0x0) {
      *param_2 = local_2c;
    }
    *param_3 = local_28;
    *param_4 = local_24;
  }
  return local_30;
}

