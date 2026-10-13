/* Firmware decompilation; not buildable replacement source.
 * Address: 0x02039e44 Name: hal_lcdDealBuffer
 */

/* SDK align: src_hal_lcd.c.o/hal_lcdDealBuffer score=1.0 segment */

void hal_lcdDealBuffer(undefined *param_1)

{
  undefined *p2;
  
  if (param_1[0x2a] == 4) {
    p2 = hal_dispframeMalloc(1);
    if (p2 == (undefined *)0x0) {
      hal_dispframeFree(param_1);
    }
    else {
      hal_rotateAdd(0,param_1,p2);
    }
  }
  else {
    ax32xx_intEnable(4,0);
    if (DAT_ram_020ce974 != (undefined *)0x0) {
      hal_dispframeFree(DAT_ram_020ce974);
    }
    DAT_ram_020ce974 = param_1;
    if (*(sbyte *)(DAT_ram_020ce980 + 0x27) == 1) {
      ax32xx_deUpdate();
    }
    ax32xx_intEnable(4,1);
  }
  return;
}

