/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020487a4 Name: exposal_adj
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: bool exposal_adj(u32 yloga_avg,u32 yloga,u32 * p_br,u32 exp_ext_mod)
   [hal/isp/hal_isp.h] */

bool exposal_adj(uint p0,uint p1,uint *p2,uint p3)

{
  bool bVar1;
  undefined1 uVar2;
  undefined3 extraout_var;
  
  bVar1 = pointer_check((undefined *)(JPEG_ram_020d7582._2502_4_ + 0xc70));
  uVar2 = 0;
  if (CONCAT31(extraout_var,bVar1) != 0) {
    uVar2 = (**(code **)(JPEG_ram_020d7582._2502_4_ + 0xc70))
                      (_DAT_ram_020d1bc8,_DAT_ram_020d1bc4,register0x00000401);
  }
  return (bool)uVar2;
}

