# Next hardware work after the successful capture audit

## Verified checkpoint

Firmware revision **90ac6b6**, live trial
`~/kc02-hw/2026-10-16/capture-audit/guarded-panel-150413/`:

- User confirms colour, live updates and **no shrinking gray bar**.
- Bounded test: 45 completed frames, zero capture guard/setup faults, zero DE
  ownership failures, stable CPU-snapshot readback.
- ISPMODE is `0x42ffff`: LCDCROP and LSEN enabled through the actual stock scaler.
- Offline checks: 400 passed, 5 skipped.
- No flash writes/erases, updater use or SD installation. Keep SD unmounted.

The camera is running the RAM takeover. Leave it running unless another hardware
trial requires a pinhole reset. The host cannot read USB results from takeover.

Read `2026-10-16-capture-audit.md` before changing capture. It records the measured
memory corruption and corrects several unsupported conclusions from earlier runs.

## Remaining work

### 1. Viewfinder layout

The first 180 rows of `camera[320x240]` contain the 320x180 sensor image; the last
60 are black. `app_render()` samples the full 240 rows into its 240x180 viewport.
That explains the **static black bottom quarter** reported by the user.

The user requested full-width 16:9. The UI-only change is now implemented:
320x180 at x=0, y=36, black above/below the image between the existing header and
footer, full-width shutter animation and corner markers near the display edges.
Capture initialization and buffer ownership are unchanged. Stored 320x240 photos
and gallery mapping are unchanged. 403 tests pass; native preview:
`~/kc02-hw/2026-10-16/capture-audit/viewport-fullwidth.png`.

**Next hardware check:** after the user resets, RAM-run this layout-only build and
confirm full width, complete image height, no gray bar and continued live colour.
Do not describe the new layout as hardware-verified until that check succeeds.

### 2. Frame rate

The user reports low FPS. Measure capture completion and presentation rates
separately; the 45 frames / 3 seconds bounded measurement is not steady-state UI
FPS. The CPU currently snapshots Y/UV, converts YUV to RGB, renders the UI and
converts/rotates RGB back into a DE buffer. Do not resume re-arm experiments on an
assumed timing mechanism.

## Current harness requirements

- Serialize USB with `flock -n "$HOME/kc02-hw/usb.lock"`.
- Use `.env/bin/python docs/hw/show_luma.py`; it pins called stock routine prefixes,
  runs a canary, verifies uploads, and allocates RAM rather than borrowing a free
  allocator window.
- Default allocation: **0x300000**. Bench extent: **0x2c0000**. Diagnostic DMA spill
  arenas account for the increase. Scratch derives from actual BSS and excludes
  the stack; never restore the old fixed scratch offset.
- Keep allocations until reset. Do not free guessed historical pointers.
- Check `capture_fault == 0`, `de_present_timeouts == 0` and stable snapshot
  readback on a bounded test before moving to a live panel trial.
- A takeover USB timeout is expected but does not prove the screen is correct.
  Obtain the user's panel verdict.
- The Logitech webcam no longer points at the panel. Ask before using it.

The earlier version of this file proposed clearing CSI interrupt-enable bit 31.
That experiment stopped event generation on hardware. Do not follow those obsolete
instructions; git history retains them as session history.
