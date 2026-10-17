# Next hardware work

## Verified checkpoints

- **`90ac6b6`** — live colour, no shrinking gray bar. Evidence
  `~/kc02-hw/2026-10-16/capture-audit/guarded-panel-150413/`. User-confirmed.
- **`5d314bf`** — full-width 16:9 viewport (320x180 at x=0, y=36). Evidence
  `~/kc02-hw/2026-10-16/capture-audit/fullwidth-panel-151400/`. User-confirmed:
  "working great, full width, all looking great."
- **`sd-readonly-155143`** — physical card is FAT32, 61,440,000 sectors, BPB read
  through the stock SD routines only. No file or sector writes.

Read `2026-10-16-capture-audit.md` before changing capture. It records the measured
memory corruption and corrects several unsupported conclusions from earlier runs.
The still/JPEG path is described in `../firmware-reference/jpeg_capture.c`.

## Immediate: 1280x720 still capture (not yet run on hardware)

The sensor delivers 1280x720; photos must be saved at that resolution, not at the
320x180 preview size. The hardware JPEG encoder makes that affordable: the ISP
writes one full frame into two planes and the encoder reads those planes directly,
so nothing is staged or resampled in software.

Implemented, tested in software, and **not yet executed on the camera**:

- `custom-firmware/src/ax_jpeg_capture.c` — polled manual-encode sequence with a
  bounded 2 s budget and a baseline-JPEG length/geometry check.
- `custom-firmware/boards/ax3292_jpeg.inc` — the RAM-only trial, wired to firmware
  routines whose prefixes `show_luma.py` verifies before upload.
- Packet word **15** = `0x4a504743` (`--jpeg-check`). Word 14 remains the SD
  photo-write token, and `--photos` now refuses to run at all.
- Beacon words 42..47 report encoder state, fault code, encoded length, CSI
  events, encoder status and the interrupt mask.

Run it bounded, read-only:

```
flock -n "$HOME/kc02-hw/usb.lock" .env/bin/python docs/hw/show_luma.py \
  --payload-dir ~/kc02-hw/2026-10-16/capture-audit/probes-photo \
  --output ~/kc02-hw/2026-10-16/capture-audit/jpeg-check-$(date +%H%M%S) \
  --jpeg-check
```

What to read from the result:

| field | meaning |
|---|---|
| `jpeg.state == 3` | encoder reported a finished frame |
| `jpeg.fault` | 1 precondition, 2 timeout, 3 CSI error, 4 encoder error, 5 output full, 6 length, 7 guard, 8 not a JPEG, 9 no source frame |
| `jpeg.length` | encoded bytes, must be > 0 and <= 0x80000 |
| `sensor-fullres.jpg` | the frame; the harness decodes it and refuses anything that is not 1280x720 |
| `jpeg-csi-regs.bin` / `jpeg-regs.bin` | the CSI/ISP and encoder register blocks as left by the trial |

Fault 9 means the CSI never reported bit 0x200: the I2W path is not signalling
frame completion and the trigger has to move to a different event. Fault 7 means
the frame DMA ran past the capture plane, which would point at the UV plane being
wider than the preview pitch suggests.

The trial is bounded and returns; it does not need a pinhole reset unless the
camera stops enumerating.

## Then: photo files, gallery, print

The uncommitted `stock_fs.c` / `stock_photo.c` work implements create-only
`LUMnnnnn.BMP` saves through the stock FatFs, a gallery reader and a queued
(cooperative) I/O state machine, all gated on `LUMA_BENCH_RUN`. Its codec is
still the 320x180 preview BMP, so it cannot be enabled as-is: the save path has to
write the encoder's JPEG (or a JPEG-decoded BMP) at 1280x720 and the gallery has to
decode that back into the viewer at a downscaled size. `--photos` refuses to run
until that replacement exists.

## Still open

### Frame rate

The user reports low FPS. Measure capture completion and presentation rates
separately; the 45 frames / 3 s bounded measurement is not steady-state UI FPS.
The CPU currently snapshots Y/UV, converts YUV to RGB, renders the UI and
converts/rotates RGB back into a DE buffer. Do not resume re-arm experiments on an
assumed timing mechanism.

### Harness requirements

- Serialize USB with `flock -n "$HOME/kc02-hw/usb.lock"`.
- Use `.env/bin/python docs/hw/show_luma.py`; it pins called stock routine prefixes,
  runs a canary, verifies uploads, and allocates RAM rather than borrowing a free
  allocator window.
- Default allocation **0x380000**, bench extent **0x340000**. BSS is now 3.17 MiB,
  mostly the full-resolution capture planes and the 512 KiB JPEG window.
- Keep allocations until reset. Do not free guessed historical pointers.
- Check `capture_fault == 0`, `de_present_timeouts == 0` and stable snapshot
  readback on a bounded test before moving to a live panel trial.
- A takeover USB timeout is expected but does not prove the screen is correct.
  Obtain the user's panel verdict.
- The Logitech webcam no longer points at the panel. Ask before using it.

The earlier version of this file proposed clearing CSI interrupt-enable bit 31.
That experiment stopped event generation on hardware. Do not follow those obsolete
instructions; git history retains them as session history.
