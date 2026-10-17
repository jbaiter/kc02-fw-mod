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
| `jpeg.picmr` | must be 0: the trial may not re-point the stock interrupt mask |
| `jpeg.note` | set when the encoded size is outside the 64..256 KiB band stock budgets for a still this wide (0x1e000..0x23000 in `hal_mjpegEncodeResolutionImage`) |
| `jpeg.decode_state` / `decode_fault` | second phase: the same frame decoded back to 320x180 by the hardware decoder (3 = done) |
| `jpeg.frame_geometry` | `(w<<16)|h` as the decoder's own header parse read it; expect `0x050002D0` |
| `jpeg.photo_selftest` | with `--photos`: 1 saved and read back, 2 refused, 3 save failed, 4 readback failed |
| `jpeg.stored_bytes` / `jpeg.files` | size of the file as re-read, and the photo count afterwards |

Add `--photos` to the same command to make that one run also:

1. encode one 1280x720 still and decode it back (phase two above),
2. save it as a **new** `LUMnnnnn.JPG` through the stock FatFs, create-only,
3. close, stop, reopen, re-read and compare byte for byte,
4. load it back through the gallery path and decode it again.

That is the whole photo pipeline in one bounded, read-only-except-one-file run. It
writes exactly one new file and never overwrites, renames or deletes anything.

Fault 9 means the CSI never reported bit 0x200: the I2W path is not signalling
frame completion and the trigger has to move to a different event. Fault 7 means
the frame DMA ran past the capture plane, which would point at the UV plane being
wider than the preview pitch suggests.

The trial is bounded and returns; it does not need a pinhole reset unless the
camera stops enumerating.

## Then: photo files and gallery

`stock_fs.c` / `stock_photo.c` now store what the encoder produces:

- A shutter press encodes one 1280x720 still (`luma_capture_photo`), then queues a
  create-only `LUMnnnnn.JPG` save through the stock FatFs. The framebuffer is never
  the source, so a "photo" can no longer be a 320x180 preview by accident.
- The save is cooperative (512-byte steps), reports only after close/SD-stop and a
  byte-for-byte readback, and fails closed: a failed or partial file is left in
  place, never deleted or overwritten.
- The gallery reads the file back into the encoder's 512 KiB window and decodes it
  with the hardware decoder straight to 320x180 (`ax_jpeg_decode.c`), so display
  downscaling costs no frame buffer. Legacy `LUMnnnnn.BMP` files still list and
  open through their old path.
- The scanner lists a file only if its size could be a still (JPEG 4 KiB..512 KiB,
  BMP exactly 320x180 or 320x240 rows), and the decoder rejects anything that is
  not one complete 1280x720 baseline frame.

`--photos` is enabled again, and still requires the read-only preflight and an
unmounted card before takeover.

**Not yet run on hardware.** The JPG, the gallery decode and the file readback all
depend on the encoder and decoder behaving as decompiled.

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
