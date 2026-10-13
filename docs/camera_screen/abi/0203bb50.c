/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203bb50 Name: hal_rtcTime2String
 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* SDK prototype: char *hal_rtcTime2String(HAL_RTC_T *rtcTime)   [hal/inc/hal_rtc.h] */

char * hal_rtcTime2String(undefined *p0)

{
  uint uVar1;
  char *pcVar2;
  
  pcVar2 = (char *)0x0;
  if (p0 != (undefined *)0x0) {
    if ((DAT_ram_020ce9fd & ' ') != 0) {
      uVar1 = (*(ushort *)p0 / 1000) / 10;
      DAT_ram_020cea00 =
           ((sbyte)(*(ushort *)p0 / 1000) - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
      uVar1 = (*(ushort *)p0 / 100) / 10;
      DAT_ram_020cea01 =
           ((sbyte)(*(ushort *)p0 / 100) - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
      uVar1 = (*(ushort *)p0 / 10) / 10;
      DAT_ram_020cea02 =
           ((sbyte)(*(ushort *)p0 / 10) - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
      uVar1 = *(ushort *)p0 / 10;
      DAT_ram_020cea04 = 0x2f;
      DAT_ram_020cea03 = ((sbyte)*(ushort *)p0 - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
    }
    if ((DAT_ram_020ce9fd & '\x10') != 0) {
      uVar1 = ((char)p0[2] / 10) / 10;
      DAT_ram_020cea05 =
           ((sbyte)((char)p0[2] / 10) - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
      uVar1 = (char)p0[2] / 10;
      DAT_ram_020cea07 = 0x2f;
      DAT_ram_020cea06 = (p0[2] - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
    }
    if ((DAT_ram_020ce9fd & '\b') != 0) {
      uVar1 = ((char)p0[3] / 10) / 10;
      DAT_ram_020cea08 =
           ((sbyte)((char)p0[3] / 10) - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
      uVar1 = (char)p0[3] / 10;
      DAT_ram_020cea0a = 0x20;
      DAT_ram_020cea09 = (p0[3] - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
    }
    if ((DAT_ram_020ce9fd & '\x04') != 0) {
      uVar1 = ((char)p0[4] / 10) / 10;
      DAT_ram_020cea0b =
           ((sbyte)((char)p0[4] / 10) - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
      uVar1 = (char)p0[4] / 10;
      DAT_ram_020cea0d = 0x3a;
      DAT_ram_020cea0c = (p0[4] - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
    }
    if ((DAT_ram_020ce9fd & '\x02') != 0) {
      uVar1 = ((char)p0[5] / 10) / 10;
      DAT_ram_020cea0e =
           ((sbyte)((char)p0[5] / 10) - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
      uVar1 = (char)p0[5] / 10;
      DAT_ram_020cea10 = 0x3a;
      DAT_ram_020cea0f = (p0[5] - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
    }
    uVar1 = ((char)p0[6] / 10) / 10;
    DAT_ram_020ce9fd = '\0';
    pcVar2 = (char *)&DAT_ram_020cea00;
    DAT_ram_020cea11 = ((sbyte)((char)p0[6] / 10) - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30
    ;
    uVar1 = (char)p0[6] / 10;
    DAT_ram_020cea13 = 0;
    DAT_ram_020cea12 = (p0[6] - ((sbyte)uVar1 * 2 + (sbyte)(uVar1 << 3))) + 0x30;
  }
  return pcVar2;
}

