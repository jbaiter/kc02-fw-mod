/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0202030c Name: present_frame
 */

void present_frame(void)

{
  if (DAT_ram_020cd6ac != 0) {
    ax32xx_sysDcacheFlush
              (*(uint *)(DAT_ram_020cd6ac + 4),
               (int)((uint)*(ushort *)(DAT_ram_020cd6ac + 0x20) *
                     (uint)*(ushort *)(DAT_ram_020cd6ac + 0x22) * 3) >> 1);
    dispLayerShow(DAT_ram_020cd6ac,0,0,0xff,register0x00000401);
    DAT_ram_020cd6ac = 0;
  }
  return;
}

