# HANDOVER: KC02 capture (CSI) — for the incoming agent

> **Latest result: read `docs/hw/2026-10-16-capture-audit.md` first.**
> The DE ownership check caught pixel data (`0x5d5d5e5d`) in the display-buffer
> index, immediately after the old capture arrays. ISPMODE was `0xffff`, with
> scaling off. The guarded build now calls the actual stock scaler, enables its
> crop/scale path, keeps DMA disabled until setup completes, and separates guarded
> DMA destinations from CPU snapshots. A bounded run completed **45 frames** with
> **zero capture faults and zero DE ownership failures**, ISPMODE `0x42ffff`, and
> a coherent full-frame Jira-board PNG. **Live trial `guarded-panel-150413` is now
> user-confirmed: colour present, image updating, no shrinking gray bar.** Frame
> rate is low. The static bottom-quarter black band comes from mapping a 320x180
> capture into a 320x240 camera buffer and sampling all 240 rows; layout is next.
> The earlier callback and exposure-only trials both left the moving bar unchanged.
>
> The current bench requests **3 MiB** and links within **0x2c0000** for diagnostic
> spill arenas. The original instructions below use outdated allocation sizes and
> overstate what was ruled out. No flash or SD installation has occurred.
>
> **Audit correction, 2026-10-16, incoming agent:** treat the original analysis
> below as session history, not established constraints. These corrections began
> before the successful guarded/scaler-enabled trial. The claim that polling
> latency must cause the bar was never established.
>
> - The automatic-mode test wrote `b018=0x20000` without `LCDEN=0x40000`.
>   No output was enabled. It did not rule out automatic capture.
> - Normal frame completion uses `hal_lcdSwitchFrame2` at **0x0203a2f0**, registered
>   for event 1 by `lcd_panel_init`. This callback was missing from the Ghidra
>   function inventory. Its body is now in
>   `docs/firmware-reference/lcd_frame_callback.c`. It writes Y and UV addresses
>   even when reusing a buffer, then calls `csiLCDDmaKick`: **OR LCDEN only**.
>   The clear/PIP-reset/set sequence is sensor-error recovery, not this path.
> - `b000=0x0440062d` has FORMAT bits 7:6 equal to zero, not YUVR422.
>   `ax32xx_csiTypeSet` at 0x02034600 confirms the encoding. The callback computes
>   UV offsets with `y/2`; the alleged 4:2:2-output/4:2:0-reader mismatch is not
>   established. Do not change the output layout on that premise.
> - `hal_csiRegister` only stores pointers; it does not configure the ISP. The
>   earlier description that sensor_init supplies a complete CSI setup is wrong.
> - Do not enable the stock CSI ISR without auditing its registered callbacks:
>   the normal callback manages stock buffers and display state.
> - `show_luma.py` now reads separate CPU snapshots and checks repeated readbacks
>   for equality. Earlier PNGs read live DMA memory and selected by chroma population.
>   Those PNGs cannot establish a rolling seam's position or cause.
>
> Callback-sequence trial: acknowledge before callback work; copy to snapshot;
> restore destination addresses; OR LCDEN. No normal-frame PIP reset. Bounded
> readback is stable, but **the user confirms the bar persists** on the panel,
> including after pointing away from the monitor. Evidence:
> `~/kc02-hw/2026-10-16/capture-audit/{baseline,callback,callback-panel}`.
>
> Further parity bugs found: takeover never applied packet exposure/gain at all;
> only the bounded entry did. Both now call `bench_apply_exposure` after bring-up
> and `bench_read_options` before it. The size override comes from host word 13,
> not 12. Previous size-override results are therefore not reliable. The parity
> fix builds and has offline regression tests; the user confirmed that this trial
> also left the bar unchanged.
>
> The static-bars A/B did not rule out scanout tearing with changing pixels.
> That old `present()` wrote the active plane; the current version double-buffers
> DE and verifies ownership before reuse. The UVC webcam no longer points at the
> panel; do not use the old
> setup instructions without asking the user to reposition it first.
>
Written 2026-10-16 at the end of a long session. Read this before touching
anything; most of it is "here is what is already ruled out and why", because this
subsystem has consumed two full sessions in a row on the same class of mistake.

## 0. Goal and hard constraints

Get Luma running from RAM on the original KC02 with a working camera viewfinder,
**without bricking the camera.**

- **RAM only.** No flash writes or erases, no SD updater, no Luma installation.
  Every hardware path in this repo writes RAM and nothing else.
- Keep the camera's SD volume unmounted.
- The original KC02 is the only hardware target. X8D/Z-12 are emulator-only.
- Recovery is the **pinhole reset**. Nothing has ever been flashed, so it always
  returns to stock. Expect to use it a lot.

## 1. Current state

**Working, verified both by measurement and by the user looking at the panel:**

- Luma runs from RAM, both bounded (`luma_bench_once`) and takeover (`_start`).
- **Live, full-frame, grayscale capture of the real scene.** It moves when you pan.
- No display tearing, no blinking. The DE path is provably clean (see §5, A/B test).

**Not working:**

- **A rolling gray bar in the upper part of the frame.** It creeps down (or up),
  then snaps back. It is the *only* significant visual defect left in the capture.
- **No colour.** The viewfinder is grayscale. Chroma plane amplitude is low
  (UV stdev ~4) and `sensor_copy()` still reads a 4:2:2 plane as if it were 4:2:0.

Do not describe the capture as "broken" — it is 90% there. Two specific defects
remain.

## 2. How to build, run, and verify

```bash
cd /home/jbaiter/.src/kc02-fw-mod

# firmware image (also builds x8d/z12 and a bench/RAM image)
python3 custom-firmware/tools/build.py
python3 custom-firmware/tools/build.py --bench-base 0x0235A200 --output /tmp/bench

# build the RAM-run payloads (ramalloc, ramfree, lumarun, probes)
python3 docs/hw/build_probes.py --output ~/kc02-hw/probes-<n>

# bounded run: returns, allocator gives a block, reads the capture planes
flock -n "$HOME/kc02-hw/usb.lock" .env/bin/python docs/hw/show_luma.py \
    --payload-dir ~/kc02-hw/probes-<n> --output ~/kc02-hw/run-<n> \
    --exp 0x100 --gain 0x100

# takeover: jumps to Luma's real _start and NEVER returns; the panel shows Luma.
# Needs a pinhole reset afterwards.
flock -n "$HOME/kc02-hw/usb.lock" .env/bin/python docs/hw/show_luma.py \
    --payload-dir ~/kc02-hw/probes-<n> --output ~/kc02-hw/run-<n> \
    --exp 0x100 --gain 0x100 --takeover

python3 -m pytest -q          # 387 pass, 5 skip
```

Useful flags: `--overlay` (draws counters on the panel), `--test-pattern`
(fills the viewfinder with colour bars instead of the imager), `--size 0x...`
(overrides `CSI_IMAGE_SIZE`/b01c).

**Operational gotchas that cost real time:**

- **Serialise USB.** Always `flock -n "$HOME/kc02-hw/usb.lock"`.
- **The camera takes up to ~50 s to enumerate after a reset.** A run that starts
  too early fails with "No KC02 camera found" while `lsusb` shows it a moment
  later. Check `lsusb | grep 0219:3280` and retry before concluding anything.
- After a takeover the camera is off the bus until a pinhole reset. Tell the user;
  do not silently retry.
- Output directories must not already exist (`show_luma.py` refuses).

**Seeing the panel yourself.** There is a Logitech UVC camera on `/dev/video4`
pointed at the KC02, which you can use to photograph the panel:

```bash
v4l2-ctl -d /dev/video4 -c auto_exposure=1,exposure_time_absolute=25   # manual
ffmpeg -loglevel error -y -f v4l2 -i /dev/video4 -frames:v 1 /tmp/panel.png
v4l2-ctl -d /dev/video4 -c auto_exposure=3,brightness=128,gain=64      # RESTORE
```

Caveats: it defaults to Aperture Priority and **blows the panel out to solid
white** unless you force manual exposure. Even then the LCD backlight blooms
small text into an unreadable mass — reading numbers off the panel optically is a
dead end. Firefox may hold the device; do not kill it. The internal webcam is
`/dev/video0`.

## 3. The measurement that matters, and its limits

`show_luma.py` reports, computed **inside the payload while Luma still owns the
buffers** (this is the only trustworthy reading — post-run reads measure the
*stock* firmware's state, since a bounded entry leaves interrupts enabled and the
stock resumes the moment the payload returns):

```
IN-RUN capture:     Y data=NNNNN/57600  UV data=NNNNN/28800
IN-RUN buffers:     readable set Y data=..., OTHER set Y data=...
IN-RUN CSI service: events_seen=0x... polls=N rearms=N
CSI raw Y:          mean / stdev / distinct / top values
```

**Three limitations that have each caused a false conclusion:**

1. **A flat plane and a featureless scene are indistinguishable.** If the camera
   is pointed at a blank wall, `within-row stdev ~1` looks identical to a dead
   capture. Before concluding the capture is broken, **point the camera at a
   fixed high-contrast target** (a printed page) or verify with the UVC camera.
   Several "capture is broken" readings in the last session were probably this.
2. **"Largest row-mean jump" is not a seam detector.** It fires on real edges in
   the scene (the desk edge, the top of a chair). Use it only for relative
   comparison between runs of the *same static scene*.
3. **The bounded run is only representative now.** Before the re-arm fix it read a
   half-written frame while the panel looked fine, which sent a whole session down
   the wrong path. If you change capture timing, re-establish that bounded and
   takeover agree **before** trusting either.

## 4. What the decompilation establishes (use it, do not re-derive)

Register names/bit fields for the whole `0xb000`–`0xb1E8` block are in
**`docs/firmware-reference/isp_csi_registers.md`** — SDK-sourced, 124 registers.
Read it before guessing at any register. Highlights:

- The block is the SDK's `SPRGROUP_SFR6`. `LS` = **LINE SCALER** (b180/b184/b188
  are its target and step; b18c+ is its anti-lsawtooth table). `I2L` = ISP-to-LCD,
  the viewfinder path (`CSI_I2L_YBADR` = b060, `UVBADR` = b064, `STRD` = b068).
- `b004` ISPMODE: a full ISP enable bitmask. Working value `0x0000FFFF`.
- `b008` ISPCTRL: bit 19 `LS_KS` is a **kickstart**, not a configuration bit.
- `b018` CSI_DMA_MODE: bit `0x40000` = `CSI_OUTPUT_LCDEN` (the "arm" bit),
  bit `0x200000` = `CSI_OUTPUT_LDMAMANEN` (**manual mode**), `0x20000` = RELOAD.
- `b00c` CSI_INTR: an index enum, so bit N is `1<<N`. **Bit 1 =
  `CSI_IRQ_LCD_FRAME_END` — the per-frame event.** Bit 4 is
  `CSI_IRQ_SEN_STATE_ERR`, an *error* bit; the stock's re-arm guard uses bit 4
  because it is an error-recovery path, which is a trap.

**The function that matters most and had never been looked at:**

```
0x02039f9c  hal_lcdPreviewStart(u16 width, u16 height)
```

It is the *caller* of every CSI routine. Its per-frame sequence is:

```c
DAT_ram_020ce97c = hal_lcdGetIdleBuffer();      /* a frame buffer, from two free lists */
hal_dispframeVideoCfg(buf, posX, posY, w, h);
ax32xx_csiLCDScaler(w, h, tar_w, tar_h, sx, sy, ex, ey,
                    *(u16 *)(buf + 0x28),       /* STRIDE from the descriptor */
                    (uint *)(DAT_ram_020ce980 + 0xd0));
ax32xx_csiOutputSet(0x20000, 0);                /* clear RELOAD  */
ax32xx_csiOutputSet(0x200000, 1);               /* set LDMAMANEN */
ax32xx_csiLCDFrameSet(*(uint *)(buf + 4), *(uint *)(buf + 8));
iVar2 = 0x100000;
do { iVar3 = ax32xx_csiScalerCheck(); if (iVar3 != 0) break; } while (--iVar2);
ax32xx_csiLCDDmaEnable(1);
```

Supporting facts:

- `ax32xx_csiScalerCheck` (0x02034e80) = `(REG(0xb008) >> 19 ^ 1) & 1` — true once
  the `LS_KS` kickstart has **self-cleared**. The vendor does not arm the DMA until
  then. Luma was arming immediately.
- `ax32xx_csiLCDDmaEnable(1)` also writes **`b00c = 0x5a`**, which is CSI_INTR's
  event mask (bits 1,3,4,6). Luma never set it. Both of these are now in Luma.
- `disp_frame_t` (from `hal_dispframeVideoCfg`, 0x02044ce0): `+0x04` Y addr,
  `+0x08` UV addr (= Y + stride*height), `+0x18` size, `+0x20` w, `+0x22` h,
  `+0x24` posX, `+0x26` posY, **`+0x28` stride = `(width + 0x1F) & ~0x1F`**.
  That formula is where the DE plane's 256-byte pitch for a 240-wide plane comes
  from. Luma's 320-wide/stride-320 is correct.
- `hal_lcdGetIdleBuffer` (0x02039f5c) → `hal_dispframeMalloc(DAT_ram_020ce961 == 0)`,
  which picks from **two** free lists (`+ frame_type * 0x98`). The vendor keeps two
  frame buffers. See §6 for why naively copying that rotation made things worse.

## 5. Established facts and dead ends — do not retry these

Each of these was measured on hardware and cost time.

| thing | result |
|---|---|
| Override the CSI block with the 13-register stock-preview sample | capture **blanked**, planes byte-identical across runs. The vendor configures **81 of 128** registers incl. ISP/dither/gamma. Let `sensor_bringup()`'s `hal_csiRegister`/`sensor_init` run **last** and keep the final say. |
| Clear `b014` bit 31 (CSI interrupt enable) | **stops event generation entirely**: `events_seen` 0x0000C00A → 0 across 4M polls. Must stay SET. |
| `b184`/`b188` left at 0 | they are the line-scaler step and must be `0x80000` for 1280x720 → 320x180 (`csiLCDScaler` computes them; Luma bypasses that function). |
| `b01c` = 1280x240 | capture dies completely. |
| `b01c` = 1280x480 | behaves **identically** to 1280x720. The imager's height is not the mismatch. |
| `csiIOConfig` (via `ax32xx_csiInit`) added on its own | makes capture **worse** (plane untouched). Not the missing piece. |
| Pause the DMA by clearing only `b018` bit `0x40000` | **not a valid stop.** A real disable (`csiLCDDmaEnable(0)`) also clears `b014` and writes `b00c`; a partial pause leaves the block unable to restart. Plane came back as the untouched fill. |
| Re-arm from inside `sensor_copy()` (~every 8 rows) to tighten latency | restarts the transfer so often no frame completes — plane flat. |
| Do not re-arm in `csi_service`; read in the idle window, re-arm after | only re-arms once per rendered frame; frames stop completing — plane flat. |
| `b018 = 0x00020000` (RELOAD set, LDMAMANEN clear), no re-arm | **kills capture.** Frame-end event never fires (`events_seen = 0xC018`, bit 1 ABSENT), plane stays at the fill. **Manual mode is required**, therefore the re-arm timing is load-bearing. |
| The stock's `b018 & 0x40000` re-arm guard, used from a poller | **never satisfiable**: the arm bit self-clears on completion, so a poller never sees it set. `rearms = 0`. |
| Gating the re-arm on event `0x10` | wrong event and unstable (0x1A in one run, 0xC00A in another). It is `SEN_STATE_ERR`. |
| Unconditional re-arm when "idle" | `REAR 121 == POLL 121` — restarts every poll, tears the frame. |
| Rotating the CSI between two buffers per frame (the vendor's scheme) | **blinking.** The vendor does this from the interrupt, exactly at the frame boundary; a poller lands mid-frame and splits the frame across both buffers. Both sets verified fully written and it still blinked. |

### The A/B test that finally localised the seam

`--test-pattern` makes `sensor_copy()` fill the viewfinder with colour bars and
**return early**, never reading the capture planes. With it:

> **no rolling bar at all**, only some green garbage at the right edge.

Therefore **the display path is clean and the rolling bar lives in the camera data
path.** Do not go looking for a DE/tearing cause.

⚠️ That test was silently void the first time it was run: `luma_test_pattern` was
only set in `luma_bench_once`, which a **takeover never calls**. `firmware_main`
reads the packet through a *different mechanism* (a hardcoded address,
`BENCH_PACKET_IN`) from the bench entry (the `buffer` argument), so words get added
to one and not the other. Words 7, 11 and 12 are now read by both. **If you add a
packet word, add it to both paths and verify the feature actually activates.**

## 6. The root problem, stated plainly

The seam is a **capture-timing** artifact: the plane contains a horizontal
discontinuity whose position **varies between runs** (measured at row 140, then
154, then 140). Something re-arms the CSI from software at a variable latency, and
that latency shifts the capture phase.

Every approach so far has been a variation of "re-arm from software at some
latency":

```
re-arm on the arm-bit guard      -> never satisfiable when polling
re-arm on event bit 4            -> wrong event, unstable
re-arm unconditionally when idle -> tears (121 re-arms / 121 polls)
rotate two buffers per frame     -> blinking (mid-frame address change)
snapshot + pause DMA             -> invalid stop, capture died
snapshot + tighter re-arm        -> frames stop completing
hardware auto-reload, no re-arm  -> no frame events at all
```

And the last experiment established the constraint that closes the easy way out:
**auto-reload does not work, so manual mode and a per-frame re-arm are mandatory.**

## 7. The best next step (not yet attempted)

**Let the stock's CSI interrupt do the re-arming.**

`ax32xx_csiIRQHandler` (0x0203404c) is still installed in memory and does exactly
this, at exactly the right moment, at zero latency. A takeover disables interrupts
with `PICMR = 0` (`REG(0x4800)`), which is why it cannot run. The CSI is IRQ 14
(`ax32xx_intEnable(0xe, ...)`; `b014` bit 31 is its enable).

So: keep `PICMR = 0` to stop the stock's display/task activity, but **selectively
unmask IRQ 14** (`REG(0x4800) |= (1u << 14)`), and stop re-arming from
`csi_service()`. Things to check while doing it:

- Does the stock ISR's own guard (`(events & 0x10) && (b018 & 0x40000)`) hold when
  it runs from the interrupt? It should — that is its design point.
- Its watchdog spins forever after 100 unserviced events. Know that before you
  enable it, and have the pinhole reset ready.
- It dispatches registered callbacks; they are null in a RAM context and it guards
  every call, but confirm rather than assume.

If that works it removes software latency from the loop entirely, which is the one
thing no variation above could do.

## 8. Colour (second defect, untouched)

`sensor_copy()` still decodes the chroma plane as 4:2:0 while the CSI format is
`CSI_TYPE_YUVR422` (decoded from `b000` bits 7:6). Prior notes call this out as the
cause of "garbled chunks" — chroma displaced across the frame. The measured plane
supports low chroma amplitude too (UV `stdev ~4`, mean ~126), which is why it reads
as grayscale. Both the layout and the amplitude are open.

## 9. Working habits this subsystem demands

Each of these was learned the hard way, twice:

1. **Decompile the CALLER before touching a register.** Two sessions were spent
   reconstructing the CSI from a 13-register sample while `hal_lcdPreviewStart` —
   one `references` query away — carried the actual answer. The callees give you
   mechanics; the caller gives you values.
2. **Read `docs/firmware-reference/isp_csi_registers.md` first.** The register
   names and bit fields are all there from the vendor headers. Guessing at them
   produced the single worst regression of the session.
3. **Do not change behaviour faster than you can measure it.** Several times a fix
   was applied before the previous one had been evaluated, and the results became
   uninterpretable.
4. **Trust the panel over the digest when they disagree**, and find out why before
   proceeding. The digest was wrong in both directions (it reported a game-over
   flat plane while the panel showed a scene, and reported a full plane when the
   harness was reading the wrong buffer).
5. **Check the scene is non-uniform before concluding the capture is broken.**
6. **Verify a diagnostic flag actually activated** before drawing conclusions from
   it. `--test-pattern` was inert for several runs.
7. Stop and say so when the ideas run out, rather than proposing a seventh
   variation of the same one.

## 10. Evidence and locations

- This session: `~/kc02-hw/2026-10-16/session-003447/` — one dir per run, each with
  `report.json`, `csi-raw.y.bin`, `csi-raw.uv.bin`, `csi-raw.png`, `csi-regs.bin`.
- Previous session: `~/kc02-hw/2026-10-15/session-202910/` — includes the stock's
  own preview captures (`preview/*/csi-*.y.bin`), which are useful ground truth for
  how much of the frame the stock fills (it puts content in the top 2/3).
- Fixture: `tests/fixtures/hardware/original-2026-10-15.json`.
- Session write-ups: `docs/hw/2026-10-15.md`, `docs/hw/2026-10-16.md`.
- Reference corpus: `docs/firmware-reference/` (`INDEX.md` is the map;
  `csi.c`, `isp_csi_registers.md` are the ones that matter here).
- Ghidra: cache `/home/jbaiter/pi-ghidra-cache`, artifact
  `218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5`,
  language `KC02:LE:32:nodelay`. The `ghidra` tool is available directly.

## 11. Repo state at handover

Branch `reversing`. Everything committed, working tree clean, `387 passed, 5 skipped`.
Nothing flashed; the SD volume was never mounted.

Relevant commits, oldest first:

```
649c1c4  Hardware session: live capture from RAM, and a firmware reference corpus
c19fce6  Settle the bounded run on the real millisecond clock
99a428e  Apply exposure after the sensor bring-up, not before
8848759  Document the CSI/ISP/line-scaler register block from the SDK
229d69c  Add the line-scaler steps, found missing by live register read
2d2e0a2  Add the vendor's scaler-ready wait and CSI_INTR mask, from hal_lcdPreviewStart
d2bc7da  Double buffer the capture planes: the image now fills the frame
7f178ce  Fix the double-buffer swap: both sets are now written
f9958f5  Restore the snapshot capture scheme; record three failed attempts at the seam
bd85ffd  Revert the auto-reload experiment: manual mode is required
```

The last two are the current, known-good-ish state: snapshot the completed frame on
the frame-end event, read that, re-arm. It yields a live full-frame grayscale image
with the residual rolling seam.
