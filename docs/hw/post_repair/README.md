# Post-repair print results (battery connector repaired)

All jobs here ran after the two-pin B+/B- connector was repaired, using the guarded RAM
harness in `docs/hw/execute_payload.py`. No flash writes. Firmware unchanged.

## What the repair changed

| Test | Before repair (USB only) | After repair |
|---|---|---|
| Dot-count staircase, 16 bands of 4..64 dots/line | only 8-11 dots/line reached the paper | all 16 bands, through 64 dots/line |
| Three 6-line full-width bars | never printed | printed across all 384 dots |

The supply limit is gone. The head now drives dense full-width loads.

## Pattern verification

Every built-in `ramprint` pattern was checked against an independent Python model of the
canvas predicate in `docs/hw/payloads/ramprint.c`. The executed ink count matched in all cases.

| Pattern | Feed lines | Ink pixels | Match |
|---|---|---|---|
| 5 staircase | 128 | 4352 | yes |
| 0 full width | 48 | 7152 | yes |
| 6 axis separation | 96 | 4144 | yes |
| 7 density vs position | 64 | 1184 | yes |
| 1 counting | 80 | 1016 | yes |
| 3 ruler | 80 | 574 | yes |
| 2 test card | 128 | 3104 | yes |

## Printing a JPEG from the card

`docs/hw/payloads/fileprint.c` opens a file with the firmware's own VFS and prints it through
the stock pipeline:

```
open(name, 1) -> fs_size(fd) -> kc02_print_file(fd, energy, layout, p6) -> close(fd)
```

Addresses: `open` `0x0206C5B8`, `close` `0x0206C680`, `fs_size` `0x0206CAF8`,
`kc02_print_file` `0x0205ADF0`. Paths are FatFs-relative, e.g. `PHOTO/PHO00045.JPG`.

| File | Bytes | Print status |
|---|---|---|
| `PHOTO/PHO00045.JPG` | 16885 | 0 |
| `PHOTO/PHO00044.JPG` | 83917 | 0 |
| `PHOTO/PHO00048.JPG` | 38466 | 0 |
| `ARTPIC.JPG` | 17122 | 0 |
| `ARTCHART.JPG` | 25056 | 0 |

Status 0 means success. The three stock photos and both generated cards printed recognisable
images, so the hardware MJPEG decode, scale-to-384 and print path all work end to end.

`kc02_load_print_image` (`0x020593CC`) return codes, recovered from the decompilation and
confirmed on hardware:

| Code | Meaning |
|---|---|
| 1 | canvas allocation failed |
| 2 | second-stage (scale) allocation failed |
| 3 | decoder rejected the JPEG bitstream |
| 4 | decode wait timed out |
| 5 | scaler setup failed |

**The decoder needs 3-component YCbCr 4:2:0.** Single-component grayscale JPEGs are rejected
with code 3. `PIL` must save as `RGB` with `subsampling=2`; `mode='L'` fails.

The stock firmware caches the FAT directory. After adding files over USB mass storage, the
firmware keeps reporting the old sizes until a power cycle. Read-back confirmed this: the same
path returned 18513 bytes (old file) before the power cycle and 17122 bytes (new file) after.

## Two payload requirements found here

1. **The entry point must be at the code-image start.** The RAM dispatcher jumps to
   `CODE_ADDR`; without `__attribute__((section(".entry")))` GCC emitted helper functions
   first and the payload ran `box()` instead of `test_entry`, returning the packet untouched.
2. **Mutable statics are usable if initialised before use.** `docs/hw/art-link.ld` links
   `.bss` inside the code window instead of asserting it empty. `objcopy` omits `.bss` from
   the `.bin`, and the payload sets every global at entry. The size assertion keeps the whole
   footprint inside the 5632-byte slot.

## Maze and resolution card

`docs/hw/payloads/artprint.c` (pattern 10 = maze, 11 = card) draws on a canvas of up to 1800
feed lines. `docs/hw/model_artprint.py` re-implements both generators on the host and computes
the same FNV-1a hash, so the device canvas is checked before paper is used.

| Pattern | Lines | Device ink | Device hash | Host model |
|---|---|---|---|---|
| maze, seed 3292 | 1624 | 38984 | `0x11948a2f` | identical |
| resolution card | 660 | 42628 | `0xc2d6fb93` | identical |

Previews: [`preview/maze_384x1624.png`](preview/maze_384x1624.png),
[`preview/maze_chest_zoom.png`](preview/maze_chest_zoom.png),
[`preview/card_384x660.png`](preview/card_384x660.png).

### Maze

Nine columns by forty rows of 5 mm cells, 360 dots across the 384-dot head, 1600 feed lines
plus a 24-line header. A 3x3 cell block in the middle row is opened into a treasure room with
a chest drawn at 111 x 81 dots. Entrances are punched top and bottom in the chest column. A
centimetre scale down the left margin marks every 1 cm with a 12-dot line and every 2 mm with
a 5-dot line.

The 20 cm length uses **8 feed steps per millimetre**, now measured on paper. The printed scale
verifies it directly, and the maze is deterministic from the seed printed in the header.

### Resolution card

Sections, top to bottom:

- `DOT TEST 384` heading.
- Full head-width bar along feed lines 44-49: its physical length is the head's width.
- A 10 mm square as a 2-dot outline and a solid twin, both 80 x 80 dots.
- An isolated dot grid at 16-dot pitch across and 8-line pitch along, 23 x 20 dots.
- Line pairs across the paper, gaps 1..12 dots.
- Line pairs along the feed, gaps 1..12 lines.
- A 10 mm and a 5 mm solid block are beside those.
- A centimetre scale and frame.

Measure these to fix the real geometry:

- the head-width bar length gives dots/mm across;
- the 80 x 80 square gives lines/mm along (expect 10.0 mm);
- the pitch grid gives the across and along pitch directly;
- the line pairs show where adjacent dots merge.

## Smallest checkerboard, full paper width (pattern 12)

A 384 x 384 line square (48 x 48 mm) filled with a 1-dot checkerboard: ink on every other head
dot, offset on alternate feed lines. That is the smallest checker the head can express, 0.125 mm
cells, and it covers the full paper width.

Below it a 24-line gap, then a ladder of six 60 x 60 dot squares with checker cells of 1, 2, 3, 4,
6 and 8 dots, each labelled with its cell size in dots. Measuring or eyeballing where the ladder
stops resolving gives the practical checker pitch.

| Item | Value |
|---|---|
| Canvas | 504 feed lines, 384 dots wide |
| Ink pixels | 84884 (the main square is exactly half of 384 x 384) |
| Device hash | `0x5748d359`, identical to the host model |
| Print status | 0 |
| Wall time | 8.1 s |

Preview: [`preview/checker_384x504.png`](preview/checker_384x504.png),
[`preview/checker_ladder.png`](preview/checker_ladder.png).

At 0.125 mm cells the isolated dots are closer than a thermal head usually resolves, so the
full-width square is expected to read as flat grey rather than as a visible checker; the ladder
shows the pitch at which the pattern actually breaks up. That is the measurement worth recording.

## Post-jam recovery run

After the jam was cleared, the mechanism was stepped back up in stages rather than restarted with
the long job:

| Step | Pattern | Lines | Ink | Canvas hash | Status | Wall time |
|---|---|---|---|---|---|---|
| report only | - | - | - | - | - | 2.9 s |
| test strip | probe | 64 | 3279 | `0xa5cb499e` | 0 | 2.9 s |
| resolution card | 11 | 660 | 42232 | `0x5b02af73` | 0 | 9.1 s |
| maze | 10, seed 3292 | 1624 | 38984 | `0x27988c73` | 0 | 18.5 s |

Every canvas hash matched the host model, and the timings reproduce the pre-jam runs almost
exactly (card 9.08 s vs 9.09 s, maze 18.5 s vs 17.4 s), so the feed mechanism is behaving the same
as before the jam. The paper sensor still read 0 after the last job, but the roll was then
exhausted, so the final maze may be truncated depending on where the paper ran out.

Timing across all jobs lands near **11 ms per feed line**, about 88 lines/s, which at 8 lines/mm is
roughly 11 mm/s of paper travel.

## Jam recovery and the diagnostic payload

The mechanism jammed after the corrected maze reprint. The firmware was not at fault:
`printer_state` (`0x020D2EC8`) read all zeros with no latched error, the camera stayed on the bus,
and the last job had returned status 0. After the operator cleared the mechanism, the readout
confirmed a clean state:

| Reading | Value | Meaning |
|---|---|---|
| `kc02_sensor_read` (`0x020593A4`) | 0 | paper present |
| `hal_gpioRead(0, 0x80)` | 0 | same read, consistent |
| cached byte `0x020E3220` | 0 | agrees |
| `kc02_battery_check` (`0x02059290`) | 14 | charging, expected while on USB |
| battery ADC / `battery_read` | 0 / 0 | expected in this state |
| app level / threshold / density | 5 / 0 / 2 | normal |
| `printer_counter` (`0x020D2EC8`) | 0 | no error latched |
| `print_sensor` (`0x020D2EAC`) | `01 01 81 80` | four byte sensor globals |

`docs/hw/payloads/probe.c` is a small diagnostic: a read-only report plus, when `INPUT[0]` is 1, a
short test strip. Every reading it takes is a pure read — `kc02_sensor_read` is just
`hal_gpioRead(0, 0x80)`, the battery calls only sample the ADC, and the rest are RAM reads. The
strip is a left-edge marker, the word `KC02`, one-millimetre feed ticks on both edges, an
across-paper reference comb and a full-width tail bar.

| Job | Lines | Ink | Canvas hash | Status | Wall time |
|---|---|---|---|---|---|
| report only | - | - | - | - | 2.9 s |
| test strip | 64 | 3279 | `0xa5cb499e` | 0 (ok) | 2.9 s |

The strip's ink count and hash match the host model, so the payload drew exactly the expected
canvas. The same readout taken once the roll ran out returned `kc02_sensor_read = 1` with the GPIO read
also 1, so the sensor does track paper: **0 = paper present, 1 = no paper**. Before that, with
paper loaded, it read 0.

One correction fell out of this work: `0x020D2EC8` had been observed as an invented 0x20-byte
"printer state" struct. The image has exactly one access to it, a 4-byte store in `print_run_A`,
and no reader at all, so the readout now reports it as a single counter. See
[../HARDWARE_OBSERVATION.md](../../HARDWARE_OBSERVATION.md).

Use the probe as the first step after any jam, and prefer a short job over a long one while the
mechanism's state is uncertain.

## Head orientation: dot 0 is the paper's right edge

Operator observation: text drawn with logical x increasing printed **mirrored**. The head shifts
dot 0 out first and that dot lands on the paper's right edge, so the canvas x axis is reversed
relative to how the sheet reads.

Fix: `artprint.c` indexes the canvas as `HEAD_DOTS - 1 - x`, so logical x = 0 is the reader's left.
Verified in the host model by rendering `F` and checking that its stem sits on the left in
paper view, then on the device by hash.

`model_artprint.py` stores the canvas in physical order (so its hash matches the device) and
flips x again when writing the preview PNG, which means **the previews show the sheet as it reads**.
The resolution card, checkerboard, tone card and maze were all reprinted with the fix.

Note this is a property of the raw canvas path. The stock `kc02_print_file` path compensates
somewhere in its decode/scale stage, which is why the printed camera photos were not mirrored.

## Practical pattern resolution (checkerboard result)

Operator observations from the printed ladder:

| Pattern | Result |
|---|---|
| 1-dot checker, 0.125 mm cells, full 384 x 384 square | reads as a **fully filled grey square**; individual dots are visible only at the square's edges |
| 2-dot checker, 0.25 mm cells | **smallest that the unaided eye can resolve** |
| 1-dot and 2-dot line pairs, gaps 1-12 dots and lines | clearly discernible |

So the printer has two different limits, and they are worth keeping apart:

- **Isolated thin features resolve at 1 dot (0.125 mm).** A single line, and the gap between two
  single lines, survives. The resolution card showed this for every gap from 1 dot upward.
- **Dense alternating patterns need 2 dots (0.25 mm).** At 50 percent coverage per line the heat
  from neighbouring dots bleeds laterally and closes the 0.125 mm gaps, so a 1-dot checker
  saturates to solid grey. 2-dot cells are the finest checker that stays legible.

The effective limit for *halftoned* content is therefore about 4 cells/mm (roughly 100 dpi of
addressable pattern), not the 8 dots/mm (203 dpi) that the head can address for isolated marks.
Single-dot detail at 203 dpi is real, but it has to be carried by lines or edges with white space
around them, not by a dense dither field.

Consequence for printing images from RAM: a one-pixel dither at 203 dpi fills the paper with
0.125 mm alternation and will flatten to grey in the mid-tones. A clustered or coarser dither with
a 2-dot minimum cell should hold more distinguishable levels. That comparison has not been
printed yet.

## Why the checkerboard went grey: per-dot grayscale

`kc02_print_head_ramp` (`0x0205A1FC`) drives the head with **32 sub-strobes per line** at
thresholds 0, 8, 16, ... 248, loading each canvas byte with `l.lbz` (zero-extended) and inking the
dot while `luma <= threshold`. Full analysis in [../PRINT_PIPELINE.md](../../PRINT_PIPELINE.md)
section 3b.

So the printer modulates heat per individual dot and has roughly 32 native tone levels. It does
not need spatial dithering at all:

- The 1-dot checkerboard went solid grey because each black dot was `0x00` and therefore received
  all 32 pulses, maximum heat, which bled across the 0.125 mm gap.
- The same 50 percent tone sent as a flat luma of 128 gets about 16 pulses per dot and should print
  as smooth grey with no pattern.

The `threshold` argument selects the mode: **0 = the 8-step ramp** (grayscale), **1 = fixed 0x80**
(a hard 50 percent cut that discards all intermediate tone and therefore needs pre-dithered 1-bit
input). The camera print path passes `app_context+0x60` here, which is the "continuous grayscale"
versus "dithered" setting.

## Tone card, printed in both threshold modes (pattern 13)

Three columns of the same 16-step ramp (white to black), 24 lines per step:

| Column | Rendering |
|---|---|
| A, dots 0-127 | true luma values, so the head applies its own 32-level ramp |
| B, dots 128-255 | 8 x 8 Bayer ordered dither, one-pixel, 1-bit output |
| C, dots 256-383 | 2 x 2 clustered dither, only 5 levels per cell |

Printed twice, the same canvas in both threshold modes, so the two modes can be compared directly
on paper.

| Job | Threshold mode | Ink pixels | Canvas hash | Status |
|---|---|---|---|---|
| ramp | 0 | 53911 | `0xd6b1272a` | 0 |
| fixed | 1 | 53911 | `0xd6b1272a` | 0 |

The canvas is identical in both jobs, which is the point: only the head's threshold source changes,
so any difference on paper comes from the mode and not from the image.

Preview: [`preview/tone_384x440.png`](preview/tone_384x440.png).

Expected readings, from the decompilation:

- **Mode 0, column A** should show the most distinct tone steps, because each dot receives a
  different pulse count. Column B should approximate the same ramp through dithering. Column C
  should show visible banding, since a 2 x 2 cell can only express five levels.
- **Mode 1** replaces the ramp with a fixed threshold of 0x80, so column A should collapse to a
  hard two-tone split at the middle of the ramp, while columns B and C, being pure 0 or 255
  already, should be unchanged.

That difference is what makes the camera's "continuous grayscale" and "dithered" settings: mode 0
keeps intermediate tone, mode 1 discards it.

## Measured calibration (operator measurements, post-repair)

| Measurement | Value | Implies |
|---|---|---|
| Resolution card width | 48 mm | 384 dots / 48 mm = **8 dots/mm across** (203 dpi) |
| 10 mm reference square | exactly 10 x 10 mm | 80 dots across = 10 mm and 80 feed lines = 10 mm, so **8 lines/mm along** |
| Centimetre scale, both cards | precise | 80 feed lines per cm confirmed on paper |
| Line pairs, gaps 1-12 dots and 1-12 lines | all clearly discernible | adjacent single dots resolve at 0.125 mm pitch in both axes |
| Resolution card, printed frame | 78 mm | frame spans 620 lines = 77.5 mm |

So the head is 203 dpi in both axes: 0.125 mm per head dot and 0.125 mm per feed step.
`LINES_PER_MM = 8` in the payloads is now measured rather than assumed, and the 20 cm maze is
1600 feed lines as intended.

Two reported figures do not fit that geometry and are worth re-measuring:

- **Maze length 220 mm.** The canvas is 1624 lines (1600 maze lines plus a 24-line header), which
  is 203 mm at 8 lines/mm, plus a 5 mm trailing feed. Either the 220 mm includes the paper consumed
  beyond the printed area, or the feed pitch during that job differed. The maze's own centimetre
  scale is the direct check: it should read 20.0 cm across the maze body.
- **Dot grid "3 mm apart".** That grid uses a 16-dot pitch across and an 8-line pitch along, so it
  should measure 2.0 mm across and 1.0 mm along. A 3 mm across pitch would imply 5.3 dots/mm and a
  72 mm head, which contradicts the measured 48 mm card width, so the across pitch cannot be 3 mm.
  Worth re-measuring to see which spacing was measured.

## Timing

| Job | Lines | Wall time |
|---|---|---|
| maze | 1624 | 18.5 s |
| resolution card | 660 | 9.1 s |
| `PHOTO/PHO00045.JPG` (384 x 384 JPEG) | 384 | 6.7 s |

About 11 ms per feed line, roughly 88 lines/s, so 8 lines/mm implies about 11 mm/s of paper
travel. USB stayed up through every job; the mailbox mirror in the payloads remains as a
fallback for a mid-job drop.

## Files

| File | Purpose |
|---|---|
| `payloads/artprint.c`, `payloads/artprint.bin` | maze and resolution-card payload |
| `payloads/gen_art_font.py`, `payloads/art_font.inc` | generated 5x7 label font |
| `payloads/art-link.ld` | link script allowing `.bss` in the code window |
| `payloads/fileprint.c`, `fileprint.bin` | print an SD-card JPEG through the stock VFS |
| `run_artprint.py`, `run_fileprint.py` | guarded runners |
| `model_artprint.py` | host model, hash check and previews |
| `make_art.py`, `art/` | generated 384 x 384 art cards |
| `staircase.json`, `fullwidth.json`, `print_*.json` | device results |

Reproduce the device check without printing:

```sh
flock -n /tmp/kc02-board-re/hw/usb.lock \
  ./.env/bin/python docs/hw/run_artprint.py --pattern 10 --lines 1624 --seed 3292 \
  --go 0 --out /tmp/maze_check.json
```

`--go 1` prints. The hosts' USB timeout must exceed the expected job time; the runners pass
180 s for JPEG jobs and 600 s for the long payloads.

## Limits

- `0x020D8000..0x020D9F54` remains the only verified-free RAM window; the payloads do not widen it.
- Nothing here touches flash. The maze and card exist only as RAM payloads and SD files.
- The 8 steps/mm feed and the 203 dpi head pitch were confirmed by measuring the printed card.
