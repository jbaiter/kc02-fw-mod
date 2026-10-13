/* Firmware decompilation; not buildable replacement source.
 * Address: 0x0203a84c Name: lcd_commit_video_frame
 */

/* Track2 evidence-based naming (likely, names_08.tsv): Takes an idle LCD buffer
   (hal_lcdGetIdleBuffer, printing "buf full" when there is none), programs the video layer through
   hal_dispframeVideoCfg and ax32xx_csiLCDFrameSet using the crop fields of DAT_ram_020ce980 /
   DAT_ram_020ce968, then kicks the LCD DMA to show the captured frame.  [was FUN_ram_0203a84c] */

void lcd_commit_video_frame(void)

{
  undefined *p0;
  
  p0 = hal_lcdGetIdleBuffer();
  if (p0 == (undefined *)0x0) {
    uart_Printf(s_buf_full_ram_02081d84,register0x00000401);
  }
  else {
    hal_dispframeVideoCfg
              (p0,*(ushort *)(DAT_ram_020ce980 + 0x38),*(ushort *)(DAT_ram_020ce980 + 0x3a),
               *(ushort *)(DAT_ram_020ce980 + 0x3c),*(ushort *)(DAT_ram_020ce980 + 0x3e));
    ax32xx_csiLCDFrameSet
              (*(int *)(p0 + 4) +
               (uint)*(ushort *)(DAT_ram_020ce968 + 4) +
               (uint)*(ushort *)(DAT_ram_020ce968 + 6) * (uint)*(ushort *)(p0 + 0x28),
               *(int *)(p0 + 8) +
               (uint)*(ushort *)(DAT_ram_020ce968 + 4) +
               (uint)(*(ushort *)(DAT_ram_020ce968 + 6) >> 1) * (uint)*(ushort *)(p0 + 0x28));
    DAT_ram_020ce97c = p0;
    ax32xx_csiLCDDmaKick();
  }
  return;
}

