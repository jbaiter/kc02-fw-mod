# Next hardware run: what changed and what to look for

Written 2026-10-16 before a session ended. Four changes were made to Luma's CSI
setup and **none of them has been tested on hardware yet**. The camera is in
whatever state the last run left it; a pinhole reset returns stock.

## What changed

`custom-firmware/boards/ax3292.c`, in `hardware_init()`. All four come from diffing
Luma against the SDK's own scaler recipe, `ax32xx_csiLCDScaler` (0x020349f8):

| # | change | why |
|---|---|---|
| 1 | `b014 = 0x000ef87b` (was `0x800ef87b`) | clears bit 31, the CSI **interrupt enable** |
| 2 | `ISPMODE(0xb004) \|= 0x20000` | `ISP_MODE_LCDCROP` — scaler crop path was never enabled |
| 3 | `b184 = b188 = 0x80000` | scaler step ratios; 1280x720 -> 320x180 |
| 4 | `ISPMODE(0xb004) \|= 0x400000` | `ISP_MODE_LSEN` — lsawtooth, before the latch |

All four were confirmed present in the built image by disassembling
`luma.bench.elf` (movhi/ori pairs, so a byte-grep of the binary will NOT find
them — that mistake wasted a cycle).

## Hypotheses these are meant to test

**#1 is the intermittency candidate.** The fixture already recorded that the stock
differs by mode: `b014 = 0x0000f818` in USB mode (bit 31 clear) versus
`0x800ef87b` in preview mode (bit 31 set). Luma copied the *preview* value. If the
stock CSI ISR can fire, it services the same transfer against its own callback
table and its own plane addresses (`0x02104080`/`0x02112180`) while Luma's polling
`csi_service()` does the same — two writers, one register set.

That also predicts the hangs: the stock handler's watchdog is
`if (100 < ++timeout_counter) { for(;;){} }`, reset only by a callback dispatch. A
spin there is a dead camera with the backlight on, needing a pinhole reset.

**#2/#3/#4 are the garbled-frame candidates.** The scaler was configured but its
crop path was never enabled and its step ratios were never set.

## How to run

    cd /home/jbaiter/.src/kc02-fw-mod
    python3 custom-firmware/tools/build.py --bench-base 0x0235A200 \
        --output custom-firmware/build/bench-original
    flock -n "$HOME/kc02-hw/usb.lock" \
        .env/bin/python docs/hw/show_luma.py --exp 0x40 --gain 0x100

Keep the SD volume unmounted, and stop on any canary or readback failure as usual.

## What to record

1. **Capture consistency**, the thing to settle first. Run it three or four times
   and note whether the raw CSI planes come back in one state or the old three
   (both planes / Y only / neither).
2. `CSI raw Y/UV` stats and `csi-raw.png` each run.
3. Whether the frame is still grey, and whether the garbling changed or went.
4. **Any hang.** If one happens, that is evidence *for* #1 rather than against it —
   note whether a pinhole reset was needed.
5. `luma_beacon[26]` (b00c) and `[27]` (b014) after the run; the beacon already
   samples both.

## If it is still intermittent

Next candidates, in order, all in `docs/firmware-reference/csi.c`:

- extract the 24-word anti-lsawtooth table and program `b18c..b1c4` — the scaler
  loads it once behind a `g_lsawtooth_loaded` flag, and Luma never loads it
- confirm whether the controller-level enable for IRQ 0x0e is still set, i.e.
  whether the stock ISR *can* fire at all. If it cannot, hypothesis #1 is wrong and
  the intermittency is something else
- port the `ax32xx_csiLCDDmaEnable` mask value (`b00c = 0x5a`) rather than leaving
  `b00c` to whatever it holds

## Unrelated but worth not forgetting

`sensor_uv[PIXELS/2]` = 38400 bytes for a 320x90 interleaved chroma plane of 28800.
Checked against three independent SDK buffer declarations (`hal_usensor.c`,
`hal_mjpeg.h`, `hal_mjpegB.c`), all of which allocate chroma at half the Y size.
Luma's geometry is correct; there is no overrun. It was suspected and cleared, so
do not re-suspect it.
