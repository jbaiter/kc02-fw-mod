/* Firmware decompilation; not buildable replacement source.
 * Address: 0x020588c0 Name: isp_set_ccm_preset
 */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* Track2 evidence-based naming (likely, names_09.tsv): Loads one of several 9-coefficient matrices
   selected by preset 1-5 into the ISP and programs it through isp_CCM_cfg.  [was FUN_ram_020588c0]
    */

void isp_set_ccm_preset(sbyte param_1)

{
  ushort *puVar1;
  ushort uVar2;
  
  puVar1 = _DAT_ram_020d73f4;
  if (*(int *)(_DAT_ram_020d1e84 + 0x18) != 0x500) {
    return;
  }
  if (param_1 == 1) {
    *_DAT_ram_020d73f4 = 0x128;
    puVar1[1] = 0;
    puVar1[2] = 0;
    puVar1[3] = 0x55;
    puVar1[4] = 0x110;
    puVar1[5] = 0;
    puVar1[6] = 0x55;
    uVar2 = 0;
  }
  else {
    if (param_1 != 2) {
      if (param_1 == 3) {
        *_DAT_ram_020d73f4 = 0x110;
        puVar1[1] = 0;
        puVar1[2] = 0x55;
        puVar1[3] = 0;
        puVar1[4] = 0x110;
        puVar1[5] = 0x55;
        puVar1[6] = 0;
        puVar1[7] = 0;
        uVar2 = 0x128;
      }
      else if (param_1 == 4) {
        uVar2 = 0x55;
        *_DAT_ram_020d73f4 = 0x55;
        puVar1[1] = 0x55;
        puVar1[2] = 0x55;
        puVar1[3] = 0x55;
        puVar1[4] = 0x55;
        puVar1[5] = 0x55;
        puVar1[6] = 0x55;
        puVar1[7] = 0x55;
      }
      else if (param_1 == 5) {
        *_DAT_ram_020d73f4 = 0x80;
        puVar1[1] = 0x80;
        puVar1[2] = 0;
        puVar1[3] = 0x80;
        puVar1[4] = 0x80;
        puVar1[5] = 0;
        puVar1[6] = 0x80;
        puVar1[7] = 0;
        uVar2 = 0x80;
      }
      else {
        *_DAT_ram_020d73f4 = 0x100;
        puVar1[1] = 0;
        puVar1[2] = 0;
        puVar1[3] = 0;
        puVar1[4] = 0x100;
        puVar1[5] = 0;
        puVar1[6] = 0;
        puVar1[7] = 0;
        uVar2 = 0x100;
      }
      goto LAB_ram_02058a8c;
    }
    *_DAT_ram_020d73f4 = 0x110;
    puVar1[1] = 0x55;
    puVar1[2] = 0;
    puVar1[3] = 0;
    puVar1[4] = 0x128;
    puVar1[5] = 0;
    puVar1[6] = 0;
    uVar2 = 0x55;
  }
  puVar1[7] = uVar2;
  uVar2 = 0x110;
LAB_ram_02058a8c:
  puVar1[8] = uVar2;
  isp_CCM_cfg(_DAT_ram_020d73f4,_DAT_ram_020d73f4[9],_DAT_ram_020d73f4[10],_DAT_ram_020d73f4[0xb]);
  return;
}

