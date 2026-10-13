/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02007744 Name: FUN_ram_02007744
 */

undefined * FUN_ram_02007744(int param_1,uint *param_2)

{
  int p0;
  undefined *p1;
  uint p0_00;
  
  p0 = nv_open(param_1);
  p1 = (undefined *)0x0;
  if (-1 < p0) {
    p0_00 = nv_size(param_1);
    *param_2 = p0_00;
    p1 = hal_sysMemMalloc(p0_00,0x40);
    if (p1 != (undefined *)0x0) {
      nv_read(p0,p1,*param_2);
    }
  }
  return p1;
}

