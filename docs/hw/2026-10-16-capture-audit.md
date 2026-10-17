# Capture audit: buffer corruption and scaler enable

## Status

RAM only. No flash writes/erases or updater use. Camera SD volume remains
unmounted. Both the guarded bounded test and live panel trial passed.

**User-confirmed on `guarded-panel-150413/`: colour is present, the image updates,
and the shrinking gray bar is gone.** Frame rate remains low. A static black band
occupies the bottom quarter of the viewfinder; its source is the current layout,
not an unresolved capture overrun. Firmware revision: `90ac6b6`.

Evidence directory: `~/kc02-hw/2026-10-16/capture-audit/`.

## Corrections to the preceding session

- The automatic-mode trial wrote `b018=0x20000` without `LCDEN=0x40000`.
  An untouched plane in that test cannot establish that manual mode is mandatory.
- Normal frame completion dispatches `hal_lcdSwitchFrame2` at **0x0203a2f0**,
  registered for event 1 by `lcd_panel_init` at 0x0203af1c. That callback was absent
  from the Ghidra function inventory. Its body is now in
  `docs/firmware-reference/lcd_frame_callback.c`.
- The normal callback restores both destination addresses even when reusing the
  current buffer, then calls `csiLCDDmaKick`, which ORs LCDEN. The clear/PIP-reset/
  set sequence belongs to sensor-size-error recovery.
- `b000=0x0440062d` has FORMAT bits 7:6 equal to **0**, not 3; DATA_BW is 1.
  `ax32xx_csiTypeSet` (0x02034600) encodes raw10 that way. Sensor input format does
  not establish the ISP output layout; the normal callback uses `y/2` UV offsets.
- `hal_csiRegister` stores pointers. It does not configure the ISP or scaler.
- Takeover did not apply packet exposure/gain; only the bounded entry did. Both
  now share `bench_read_options` and `bench_apply_exposure`, after sensor bring-up.
  Host word 13 contains the size override; reading word 12 made previous size
  sweeps ineffective.
- Static colour bars do not rule out tearing when pixels change between frames.
  The old `present()` wrote directly into the scanned plane.
- Earlier raw PNGs read live DMA memory and selected samples by chroma population.
  Those files cannot establish a seam's position. Read a stable CPU snapshot.

The user confirmed that the callback-sequence trial and exposure-parity trial
both left the gray bar unchanged. The user also reported persistence after
pointing away from the monitor. Neither trial solved it.

## Direct evidence of memory corruption

Adding a guarded DE swap exposed a failure before the first bounded presentation:
`lcd_submitted`, which software only assigns 0 or 1, contained pixel data.

Run: `de-bounded-143035/`.

- `lcd_submitted` address: `0x023935a8`.
- Raw post-run scalar bytes, saved as `post-run-lcd-scalars.bin`:
  `5e5e5c5d5c5d5c5c5d5e5d5d5d5d5d5d5d5d5d5d5d5e5e5e5e5e5f5e5f5e5e5e`.
- The index word is **0x5d5d5e5d**. Its neighbouring bytes look like luma pixels.
- The old two Y arrays occupied `0x023773a0..0x0239359f`. The corrupted scalars
  followed them. No CPU snapshot copy should reach those scalars.
- Consequently the beacon's computed display pointers became `0xb82eb5c0` and
  `0x5d3355c0`. These were not valid allocated framebuffer addresses.

CSI DMA overrunning its reserved frame is the leading explanation. In the same
configuration, ISPMODE was `0xffff`: **both LCDCROP and LSEN were off**, despite
programmed target dimensions and step registers. The original initialization
also armed LCD DMA before sensor bring-up and buffer initialization.

## Current safety and initialization changes

1. Stop inherited CSI core/output before touching buffers.
2. Separate DMA destinations from CPU snapshots. `sensor_y` and `sensor_uv` now
   name **only** CPU snapshots; the manifest identifies the new layout explicitly.
3. Put eight-word guards immediately after each expected DMA plane. Bench builds
   also reserve spill space for a full unscaled 1280x720 Y frame / half-height UV
   frame, plus a row margin and tail guards. A guard fault disables core/output
   and prevents copying or kicking another frame.
4. Keep output disabled while initializing guards and sensor.
5. Call the actual stock `ax32xx_csiLCDScaler` at **0x020349f8**, using the panel
   descriptor's table at `*(u32 *)0x020ce980 + 0xd0`. This enables LCDCROP and LSEN,
   sets crop/target/ratios/stride, loads coefficients if needed, and kicks LS_KS.
6. Enable core frame clocks with output still disabled; wait boundedly for LS_KS
   to clear. Refuse to arm if the crop/scaler-enable, size or stride check fails.
7. Arm output last. Keep the normal frame callback sequence, not PIP error reset.
8. Double-buffer DE Y/UV. Wait for commit clear **and** the expected active Y
   readback at c87c before reusing the old plane. Draw diagnostic marks before
   submission, never into a submitted/active buffer. Reject corrupted indices
   before forming an array pointer.

The harness pins 48-byte prefixes of allocator, sensor-init, exposure-writer and
scaler routines against the canonical firmware before execution. These extra read
approvals do not approve writes to firmware addresses. Canary and upload readback
checks remain in place. The harness no longer frees guessed historical pointers.

Bench link extent is now **0x2c0000**, with a **0x300000** allocator request to
hold diagnostic spill arenas. Standalone images retain their 2 MiB link budget.
Scratch follows this build's BSS with alignment and a 16 KiB stack exclusion; the
old fixed scratch offset would overlap the larger framebuffer allocation.

## Guarded bounded result

Run: `guarded-bounded-144138/`; log: `guarded-bounded.log`.

| measurement | result |
|---|---|
| firmware signatures, canary, uploaded-image readback | passed |
| completed capture frames / kicks | 45 |
| CSI events seen | `0x0000000a` (frame-end + sensor statistics) |
| capture guard/setup faults | 0 |
| DE ownership timeouts | 0 |
| ISPMODE b004 | **0x0042ffff** |
| LS_KS b008 | 0 after setup |
| stride b068 | 320 |
| target b180 | `0x00b40140` (320x180) |
| steps b184 / b188 | `0x80000` / `0x80000` |
| stable CPU-snapshot readback | passed repeated equality check |
| Y mean / standard deviation | 94.2 / 35.4 |
| Y distinct byte values | 180 |

The PNG shows a coherent full-frame Jira board. After a fresh reset, takeover
`guarded-panel-150413/` passed firmware signature checks, canary execution and image
readback. The user then confirmed colour, live updates and no shrinking gray bar.
Both scaler setup and display ownership changed before this successful trial;
that combined result does not isolate either change as the sole cause of the bar.

## Remaining layout and performance issues

`sensor_copy()` fills the first 180 rows of the 320x240 RGB camera buffer and
writes black to the last 60. `app_render()` samples all 240 source rows into its
240x180 viewport. Thus **60/240 = 25%** of the viewport is black at the bottom.
The user requested full display width with black above/below as needed. The UI
now copies those 320x180 valid rows at native size into x=0..319, y=36..215,
between the existing navigation header and footer. Black fills the remaining
content rows. Corner markers and shutter animation follow the new full width.
Stored 320x240 photo data and its 4:3 gallery mapping remain unchanged.

Native pixel tests verify full-width first/last/middle rows, black letterbox
rows, shutter coverage and unchanged gallery mapping. The native render of the
saved capture is `viewport-fullwidth.png`.

This layout-only change did not alter capture/DMA setup, and it is **confirmed on
the panel**: trial `fullwidth-panel-151400`, evidence
`~/kc02-hw/2026-10-16/capture-audit/fullwidth-panel-151400/`. The user's verdict
was "working great, full width, all looking great."

The user reports low FPS. The bounded settle observed 45 capture completions in
3 seconds, but that does not measure steady-state UI FPS. Measure capture and
presentation rates separately before changing capture timing or optimizing copies.

## Offline checks

`400 passed, 5 skipped`, plus successful builds for all three standalone targets
and the original-board bench image. Native tests execute the production CSI
service and presenter with fake registers, check DMA guards in both memory
layouts, check entry-point option/exposure parity and reject unsafe scratch ranges.
These tests do not model silicon timing or prove physical display quality.

## Webcam setup

The Logitech webcam no longer points at the panel. Do not rely on the old handover
instructions. An unintended short capture was deleted; original webcam controls
were restored. Ask the user to reposition it before using it for panel evidence.

## Still capture at sensor resolution (2026-10-16, implemented, not yet run)

Saving the 320x180 preview is not a photo. The sensor delivers 1280x720, and the
hardware JPEG encoder can produce that without a software resample, because the
stock manual-encode still branch feeds the encoder from the two Y/UV planes the ISP
DMA has just written:

- `ax32xx_csihv_tomjp_dma_cfg` (0x02034308) points the I2W frame DMA at the planes
  and `CSI_OUTPUT_WIFIEN` (0x100000) starts it. `CSI_OUTPUT_MJPGEN` (0x80000)
  belongs to the *other* still branch, which streams through a rolling
  `HAL_CFG_MJPEG_HEIGHT`-line buffer and needs the ISR-driven ring.
- `jpeg_csi_frame_callback` (0x0204062c), registered at CSI status bit 0x200, does
  one hardware action: `ax32xx_mjpeg_manual_on` (0x02036b9c) writes
  `PRE_JPELINECNT` and sets `JERUN`. A polled image can wait for that bit and make
  the same call instead of installing the handler.
- `ax32xx_hvScaler` only accepts 2:1, 4:1, 3:2 and 3:1, so a 1:1 capture cannot use
  it; its body still documents the no-scale register state (`CSI_I2W_HCROP/VCROP`
  with zero steps).

`custom-firmware/src/ax_jpeg_capture.c` runs that branch polled: arm, wait for
0x200, check the capture-plane tail guards, flush the planes, trigger, then require
`PRE_JPFLG` frame end, an output length inside the 512 KiB window, and exactly one
complete baseline JPEG whose SOF says 1280x720. It then restores the preview
exactly as `hardware_init` established it.

The frame never passes through the CPU and no extra full frame is allocated: the
planes are the existing `sensor_dma_*` spill arenas, which were already sized for
an unscaled frame so that a bad scaler configuration trips a guard. Only the
512 KiB encoder window is new BSS.

Open questions only a hardware run answers: whether the I2W crop registers suffice
without toggling `ISP_MODE_SDEN`, whether the UV plane is 1280-wide × 360 rows or
tightly packed 640-wide, whether bit 0x200 is really the I2W completion signal on
this sensor configuration, and the encoded size at quality 4.

Software checks: 11 new native tests execute the production capture state machine
against fake registers and a synthetic baseline JPEG (arming order, trigger gating,
timeout, guards, output error/pause, non-JPEG refusal, precondition refusal), and
the full suite is `433 passed, 5 skipped`.
