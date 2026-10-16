# Capture audit: buffer corruption and scaler enable

## Status

RAM only. No flash writes/erases or updater use. Camera SD volume remains
unmounted. The guarded bounded run passed; the live panel trial is pending.
Do not report the moving gray bar as fixed until the user checks it.

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

The PNG shows a coherent full-frame Jira board. This verifies a bounded capture
and removes the observed corruption in that run. It does **not** verify live motion
or the absence of the moving bar. A fresh-reset takeover remains the next check.

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
