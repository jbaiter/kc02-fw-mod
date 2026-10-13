# 383-vs-384 print test — procedure and interpretation

## Why this test

`kc02_print_head_ramp` (`0x0205A1FC`) and the byte-identical loop in `kc02_print_run_B` shift the head
data out with:

```
0205a4e4: ori   r4,r24,0        ; r4 = stride (image width = number of paper lines)
0205a4ec: lwz   r5,4(r1)        ; r5 = stride * rows
0205a4f0: sfltu r4,r5
0205a4f4: bnf   0x205a558       ; loop while r4 < stride*rows
...
0205a504: lbz   r5,0(r5)        ; dot = buf[r4 + line]
```

`r4` starts at `stride` (index 1) and stops before `stride*rows`, so **exactly `rows-1` = 383 clocks are
emitted per line and bitmap index 0 is never driven**. Either the head really has 383 usable dots (index 0
being a dummy/padding column), or the vendor code carries a one-dot offset that shifts every printed line
by ~0.125 mm (203 dpi) and leaves one head dot holding stale data.

## Pattern

`t384x384.jpg` (also `t384x320.jpg`, `t672x384.jpg`; `.png` files are previews only). Features, chosen so
the answer is readable with a ruler rather than a microscope:

* **2-px black frame at the very edge** — the primary signal: if column 0 is never driven, the left frame
  line is missing from the print (and the whole image sits one dot further left).
* **Ticks every 8 px** (2 px wide, 8 px long; every 8th tick long) along top and bottom — measure the
  distance from the paper edge to the first tick: **~0.125 mm if column 0 printed, ~1 mm if it was
  dropped**. That is the ruler-scale discriminator.
* **Control lines at x=4 and x=379** inside the frame — these must always print.
* **16x16 block top-left, centre cross, dashed mid line, 7-segment "384"** — orientation and mapping
  (which image axis becomes the paper feed).
* Tick pitch should measure **~1 mm**; count the long ticks to see whether 48 or 47 intervals fit.

## Procedure

1. On the PC, copy `t384x384.jpg` into the **`PHOTO/`** folder of the camera's SD card (firmware folder
   layout: `PHOTO/`, `VIDEO/`, `AUDIO/`). Keep `DestBin.bin` off the card or the camera will start a
   firmware update.
2. Plug the camera into USB and leave it on its USB/SD screen. Then the host requests **mode 4 (Play
   Back)** with the mode payload (`execute_payload.py --payload mode --mode 4`). The USB device will drop
   (camera mode does this too) - that is expected; the camera then shows the playback browser.
3. In playback, select the test file and use the print function (mode-4 message `0x1A` = print file).
4. Photograph or scan the print **together with a ruler**, and copy that image to the PC so the geometry
   can be measured in pixels.

If the playback browser does not list the copied file, the file wrapper is the reason: the camera stores
photos in its own record format (`kc02_print_file` validates a 12-byte record header containing the `JRX`
magic). Fallback that still answers the edge question: display the pattern on a screen and **photograph it
with the camera itself**, then print that photo - the printer's column-0 behaviour is independent of the
source image, so the frame test remains valid (only the tick-pitch measurement degrades).

## Interpretation

| observation | conclusion |
|---|---|
| left frame line **missing**, first tick ~1 mm from the edge | column 0 is never clocked: **383 dots are driven**, the image is shifted one dot, and the one unclocked head dot holds stale data — a real vendor offset worth fixing in custom firmware |
| left frame line present, first tick ~0.125 mm from the edge | the head's active width really is 383 dot columns (index 0 = deliberate dummy) or the buffer's column 0 is padding — **the loop is correct** |
| a **top or bottom** frame line is thin/missing instead | the skipped index maps to the *paper-feed* axis, not the head axis — the model in §3.1 needs its axes swapped |
| tick pitch ≠ ~1 mm, or 47 intervals fit | the print pipeline scales/normalises differently than assumed (`384 x anything` kept vs rescaled) — check `0x020594F0`-`0x02059608` against the measured width |
