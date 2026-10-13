/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0205609c Name: record_config_set
 */

/* pipeline map: mjpeg/audio parameters */

undefined4 record_config_set(int param_1,int param_2)

{
  undefined1 uVar1;
  undefined1 uVar2;
  undefined1 uVar3;
  undefined1 uVar4;
  undefined1 uVar5;
  undefined1 uVar6;
  undefined1 uVar7;
  undefined1 uVar8;
  undefined1 uVar9;
  undefined4 uVar10;
  
  uVar10 = 0xffffffff;
  if (JPEG_ram_020df023._16733_1_ == 0) {
    uVar2 = JPEG_ram_020d7582._15288_1_;
    uVar3 = JPEG_ram_020d7582._15289_1_;
    uVar5 = JPEG_ram_020df023._16799_1_;
    uVar6 = JPEG_ram_020df023._16800_1_;
    uVar8 = JPEG_ram_020df023._16802_1_;
    uVar9 = JPEG_ram_020df023._16821_1_;
    if (param_1 == 2) {
      JPEG_ram_020d7582._15298_4_ = param_2 * 1000;
      uVar4 = JPEG_ram_020d7582._15290_1_;
      uVar7 = JPEG_ram_020df023._16801_1_;
      JPEG_ram_020df023._16809_4_ = JPEG_ram_020d7582._15298_4_;
    }
    else {
      uVar1 = (undefined1)param_2;
      uVar4 = uVar1;
      uVar7 = uVar1;
      if (((param_1 != 3) &&
          (uVar3 = uVar1, uVar4 = JPEG_ram_020d7582._15290_1_, uVar6 = uVar1,
          uVar7 = JPEG_ram_020df023._16801_1_, param_1 != 4)) &&
         (uVar2 = uVar1, uVar3 = JPEG_ram_020d7582._15289_1_, uVar5 = uVar1,
         uVar6 = JPEG_ram_020df023._16800_1_, param_1 != 5)) {
        if (param_1 == 6) {
          JPEG_ram_020df023._16815_2_ = SUB42(param_2,0);
          JPEG_ram_020df023._16813_2_ = SUB42((uint)param_2 >> 0x10,0);
          uVar2 = JPEG_ram_020d7582._15288_1_;
          uVar5 = JPEG_ram_020df023._16799_1_;
        }
        else {
          uVar2 = JPEG_ram_020d7582._15288_1_;
          uVar5 = JPEG_ram_020df023._16799_1_;
          uVar8 = uVar1;
          if ((param_1 != 7) && (uVar8 = JPEG_ram_020df023._16802_1_, uVar9 = uVar1, param_1 != 8))
          {
            return 0xffffffff;
          }
        }
      }
    }
    JPEG_ram_020df023._16821_1_ = uVar9;
    JPEG_ram_020df023._16802_1_ = uVar8;
    JPEG_ram_020df023._16801_1_ = uVar7;
    JPEG_ram_020df023._16800_1_ = uVar6;
    JPEG_ram_020df023._16799_1_ = uVar5;
    JPEG_ram_020d7582._15290_1_ = uVar4;
    JPEG_ram_020d7582._15289_1_ = uVar3;
    JPEG_ram_020d7582._15288_1_ = uVar2;
    uVar10 = 0;
  }
  return uVar10;
}

