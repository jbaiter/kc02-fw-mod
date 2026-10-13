/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0200c698 Name: timerMsgSend
 */

/* SDK align: taskSysScanDevice.c.o/timerMsgSend score=1.0 segment */

void timerMsgSend(void)

{
  undefined *puVar1;
  int iVar2;
  
  puVar1 = hal_rtcTimeGet();
  if (UNK_ram_020c3350 != puVar1[6]) {
    app_context._40_4_ = app_context._40_4_ + 1;
    UNK_ram_020c3350 = puVar1[6];
    XMsgQPost((undefined *)app_context._116_4_,(undefined *)0xa);
    taskVideoRecTimeIncreace(register0x00000401);
    resBuffConut();
  }
  iVar2 = XOSTimeGet();
  if (500 < (uint)(iVar2 - UNK_ram_020ccdec)) {
    UNK_ram_020ccdec = XOSTimeGet();
    XMsgQPost((undefined *)app_context._116_4_,(undefined *)0x9);
  }
  return;
}

