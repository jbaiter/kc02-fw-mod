# Still capture: full-resolution JPEG through the hardware encoder

The preview path and the still path are separate hardware configurations that
share the CSI/ISP block. This records what the stock firmware actually does, as
read from `firmware/original.bin`, and why Luma's RAM image uses the branch it
uses.

## The two still branches in `FUN_02040c6c`

`photo_encode_frame` (0x020087e8) is the still-photo entry. It calls
`hal_csiResolutionGet` for the sensor size, then `FUN_02040c6c(w, h, quality,
watermark, decode_flag, mode)` with the sensor size — *not* the requested photo
size. Inside, the branch depends on whether the requested encode size is smaller
than the sensor:

```
if (req_w < csi_w || req_h < csi_h) {
    /* A: downscale */
    hal_mjpegEncodeInit();
    mjpeg_quality_ctl_init(quality);
    ax32xx_hvScaler(csi_w, csi_h, req_w, req_h);          /* integer ratios only */
    ax32xx_csihv_tomjp_dma_cfg(y, uv, csi_h, csi_w);      /* full-frame Y/UV  */
    hal_streamInit(...); cache_pre_malloc(...);
    ax32xx_mjpegEncodeBufferSet(out, out+len);
    ax32xx_mjpegEncodeInit(true, false);                  /* MANUAL           */
    ax32xx_mjpeg_inlinebuf_init(y, uv);
    ax32xx_mjpegEncodeSizeSet(csi_w, csi_h, req_w, req_h);
    ax32xx_mjpegEncodeQuilitySet(q);
    ax32xx_mjpegEncodeInfoSet(0);
    csiISRRegiser(0, NULL);        /* JPG_FRAME_END   */
    csiISRRegiser(3, NULL);        /* SEN_STATE       */
    csiISRRegiser(9, jpeg_csi_frame_callback);
    mjpegEA_ncodeISRRegister(jpeg_encoder_callback);
    csiOutputSet(CSI_OUTPUT_WIFIEN, 1);
    csiEnable(1);
} else {
    /* B: no scaling — streaming */
    ax32xx_csiScaler(csi_w, csi_h, req_w, req_h, 0,0, csi_w, csi_h);
    ax32xx_csiMJPEGFrameSet(y, uv, HAL_CFG_MJPEG_HEIGHT, HAL_CFG_MJPEG_WIDTH);
    ax32xx_mjpegEncodeInit(false, false);                 /* AUTO             */
    ...
    csiISRRegiser(0, jpeg_csi_frame_callback);
    csiISRRegiser(9, NULL);
    mjpegEA_ncodeISRRegister(0x0203f680);
    csiOutputSet(CSI_OUTPUT_MJPGEN, 1);
}
```

Three things follow, and each one contradicts an assumption that was easy to
make:

1. **`WIFIEN`, not `MJPGEN`, drives the frame DMA in the manual branch.** The
   ISP output goes to memory through the I2W DMA (`CSI_I2W_YBADR`, `..._UVBADR`,
   `..._LBC`, `..._LBS`) at 0x02034308, and the *encoder* reads those two planes
   from SDRAM via `PRE_JPIBAS`/`PRE_JPIEND` when it is triggered. `MJPGEN`
   belongs to the streaming branch, where CSI feeds the encoder directly through
   a rolling `HAL_CFG_MJPEG_HEIGHT`-line buffer.
2. **`hal_mjpegPhotoStart`'s own header comment is misleading.** Its manual
   branch is the low-RAM, single-shot design: one full frame into two planes, one
   manual encode, no ring buffer, no `hal_streamIn`. The streaming branch is the
   one that needs `cache_pre_malloc` and an ISR that keeps refilling the encoder.
3. **`ax32xx_hvScaler` refuses a 1:1 capture** — it only accepts 2:1, 4:1, 3:2 and
   3:1 integer ratios, and returns `false` for equal sizes. Its body is still the
   right description of the "no scaling" configuration: clear SDEN and WIFICROP,
   write `CSI_I2W_HCROP = obj_w<<16 | steps<<11 | 0`, the same for V, then set
   SDEN. For a full-frame capture the steps are 0.

## Manual encode trigger

`jpeg_csi_frame_callback` (0x0204062c) is registered at CSI ISR index 9
(`CSI_IRQ_VXY_FRAME_EN`, status bit 0x200). Its only hardware action is
`ax32xx_mjpeg_manual_on()` (0x02036b9c):

```
PRE_JPELINECNT = 0;        /* 0xb8a4 */
PRE_JPCON1    |= 0x100;    /* 0xb800 bit 8, JERUN */
PRE_JPELINECNT = 8;
PRE_JPELINECNT = 800;
```

So a polled image can do the same thing without installing a handler: wait for
status bit 0x200 in `0xb00c`, stop the I2W writer (`0xb018 &= ~0x100000`), then
call 0x02036b9c. Everything else in that callback is display/effect work that has
no business running from a benchmark.

## What the encoder needs set

| register | value | source |
|---|---|---|
| `0xb800` (`PRE_JPCON1`) | tables + `BIT(22)` DCTIDCT, `BIT(13)` ENCHOLD, `BIT(11)` JESTSYNC, `BIT(14/15)` critical, `BIT(0)` JEODMAEN, `BIT(10)` manual | `ax32xx_mjpegEncodeInit(1,0)` |
| `0xb820/0xb824` | encoder input = the captured Y and UV planes | `ax32xx_mjpeg_inlinebuf_init` |
| `0xb878` / `0xb80c` | source size / target size, `(w<<16)|h` | `ax32xx_mjpegEncodeSizeSet` |
| `0xb87c/0xb880` | integer + fractional scale steps | same |
| `0xb800` bits 23..26 | quality, `JPEG_Q_75 = 4` | `ax32xx_mjpegEncodeQuilitySet` |
| `0xb800` bits 19..20 | info mode 0 = plain JPEG (mode 1 = AVI index) | `ax32xx_mjpegEncodeInfoSet` |
| `0xb810/0xb814` | output window, end address OR-ed with `0x30000000` | `ax32xx_mjpegEncodeBufferSet` |
| `0xb83c` (`PRE_JPINTCON`) | `BIT(31)|BIT(6)|BIT(4)|BIT(2)|BIT(0)|BIT(11)|BIT(10)|BIT(9)` | `EncodeInit` |

Output length for one frame is `PRE_JPOLTA` (0xb81c); `jpeg_snapshot_callback`
falls back to `PRE_JPOPTR` (0xb818) when it reads zero. Status is `PRE_JPFLG`
(0xb840); `BIT(6)` is frame end, `BIT(0)` is output pause and `BIT(3)` is output
error, which is exactly the pair `hal_mjpegCallback_state` accepts and rejects.
`PRE_JPFLG_CLR` (0xb8b0) is written with all ones by `EncodeInit`.

The stock still buffer is `hal_sysMemMalloc(0x80000, 0x40)` — 512 KiB — and the
still path gives the encoder 2000 ms to produce a frame.

## Luma's use of it

`custom-firmware/src/ax_jpeg_capture.c` runs branch A without its ISRs, streams
or display effects:

1. Save `0xb000/0xb004/0xb014/0xb018/0xb038/0xb03c`, drop LCDEN and MJPGEN, stop CSI.
2. `EncodeInit(manual=1, tp=0)`, full-window I2W crop, `csihv_tomjp_dma_cfg`,
   `inlinebuf_init`, `SizeSet(1280,720,1280,720)`, quality 4, info 0, output
   window, then `WIFIEN` and `csiEnable(1)`.
3. Poll `0xb00c` for 0x200. Check the capture plane tail guards. Flush the
   planes from the data cache. `manual_on()`.
4. Poll `0xb840` for frame end. Refuse output error/pause, refuse a length
   outside the 512 KiB window, refuse any byte range that is not exactly one
   complete baseline JPEG whose SOF says 1280x720.
5. Restore the preview: LCDEN off, `csiLCDScaler` again, restore `0xb060/0xb064`,
   `0xb014`, `0xb000`, ack `0xb00c`, LCDEN on.

Nothing is copied between capture and encoding: the encoder reads the same two
planes the ISP DMA wrote. That is the whole point of the manual branch, and it
is why a full-resolution still needs no extra frame buffer beyond the capture
planes that already exist to catch scaler overruns.

## RAM cost

| item | bytes |
|---|---|
| Y plane, 1280x720 | 921,600 |
| UV plane, 1280x360 | 460,800 |
| JPEG output window | 524,288 |

The two planes are the existing `sensor_dma_*` spill arenas, sized earlier to hold
an unscaled frame so a bad scaler configuration trips a guard instead of
corrupting Luma state. The output window is `luma_jpeg_output`, BSS, guarded
before and after. Total bench image BSS is 3.17 MiB of the 3.4 MiB extent, and
the allocator request is 3.5 MiB.

## Not yet established on hardware

Everything above is decompiled or SDK-read. The bounded `--jpeg-check` trial has
not run yet, so these remain open:

- whether the I2W crop registers alone are enough, or `ISP_MODE_SDEN` must be
  toggled around them as `ax32xx_hvScaler` does;
- whether the UV plane is 1280-wide × 360 rows (as the preview's UV pitch
  implies) or tightly packed 640-wide;
- whether status bit 0x200 is the frame-completed signal for the I2W path on
  this sensor configuration, or only a frame-start enable;
- the real encoded size at quality 4 for a scene like the panel's.
