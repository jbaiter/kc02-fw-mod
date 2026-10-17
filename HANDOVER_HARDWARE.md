# Handover: Luma and the hardware bring-up

This is the authoritative handover for the **Luma / bring-up / USB / update**
workstream, and for the **hardware session** that is about to start.

## Latest result — 2026-10-16, capture audit

**Supersedes older status and experiment instructions below.** Revision `90ac6b6`
runs from RAM with **user-confirmed live colour capture and no shrinking gray bar**.
Read `docs/hw/2026-10-16-capture-audit.md` and the correction at the top of
`HANDOVER_CAPTURE.md`. A guarded DE swap exposed pixel data in scalars after the
old capture arrays. The current setup calls the actual stock scaler, enables
LCDCROP/LSEN, guards DMA destinations, uses separate CPU snapshots and double-
buffers DE scanout. The bounded run completed 45 frames with zero capture/DE
faults; live evidence is `~/kc02-hw/2026-10-16/capture-audit/guarded-panel-150413/`.
400 tests pass; no flash writes/erases or SD installation occurred.

Remaining: low FPS (measure before optimizing). The static bottom-quarter black
band came from sampling padding in the 320x240 RGB camera buffer. The full-width
320x180 UI is implemented, native-tested and **user-confirmed on the panel**
(trial `fullwidth-panel-151400`), with black above/below and the header/footer
retained. Capture/DMA setup is unchanged by it. Do not reset or replace the
working image without a reason; the camera needs a pinhole reset only after a
takeover.

Still capture now targets the sensor's own **1280x720** through the hardware JPEG
encoder, driven entirely from the stock manual-encode branch
(`docs/firmware-reference/jpeg_capture.c`). Bench requests **3.5 MiB** and links
within **0x340000**: the spill arenas hold one unscaled frame and the encoder
window is 512 KiB. Scratch follows actual BSS with a stack exclusion. The
`--jpeg-check` trial has not run on hardware, and no SD photo file has been
written. Older allocation sizes, the earlier claims that auto mode is impossible
or that output is 4:2:2, and the preview-BMP save design are not reliable.

## Latest bench handoff — 2026-10-15, 21:27

**This update supersedes older status/next-step text below. Hardware experiments
stopped at the user's request/frustration with elapsed time. Do not resume or
launch Luma without a fresh user instruction. No flash program/erase ran.**

- Stock camera preview worked, visually confirmed by the user. RAM USB hoist
  worked. CSI measured **320×180 / pitch 320**; DE video **180×320 / pitch 192**,
  centered at portrait x=30 on the 240×320 panel.
- Old packet `020d9700..020d9eff` is **unsafe in preview**: `020d9c00` changed to
  `3`; verification refused execution. Bench runner now uses **packet 020d8800,
  2048 bytes; code 020d8100, maximum 1792 bytes**. Candidate survived passive
  surveys and CPU/host full-packet FNV comparison. General toolchain harness now
  refuses non-USB mode or an active hoist before uploads; its old layout is
  USB-idle-only. Do not bypass these guards.
- Shutdown observations differ: the user reported one orderly shutdown, but on
  the later USB disappearance the **camera remained alive in viewfinder**.
  Cable replug recovered USB without reboot. Do not equate USB loss with power-off.
- Configured auto-off is **300 seconds**. Optional `usb-hoist --keep-awake` uses
  a 48-byte RAM stub clearing only `020ccdcc`; no saved-setting/battery-protection
  change. Its idle counter stayed zero throughout the measured 30-second preview
  observation. Sustained USB reliability beyond that remains unqualified.
- `docs/hw/preview_session.py` holds one interface claim across payloads and DMA
  reads, using the shared canary/readback/cache guards. It captured two changing
  CSI and DE frames and 31 successful heartbeats. Images were nearly black;
  bright-scene colour/orientation validation remains pending.
- Returning to mode 10 **re-enumerates USB** (024→025 observed). The first session
  reported failure on the departing handle; a separate fresh-handle probe
  confirmed mode 10 and advancing uptime, with no reboot. The script now contains
  fresh-identity reconciliation, but that revised end-to-end path is **not yet
  hardware-tested**. No automatic replay of the mode request.
- Latest evidence directory is in `~/kc02-hw/current-preview`; raw session data:
  `~/kc02-hw/2026-10-15/session-202910/preview/attempt-205630/reboot-210346/`.
  `single-session/` contains registers, raw Y/UV, PNG/PPM and the original failed
  session report; `usb-reenumerated-state/` records successful reconciliation.
- Emulator changes: earlier JEDEC/ADC/TT fixes plus DE address masking and SDRAM
  translation. Hardware DE addresses are 24-bit offsets; CSI uses full CPU
  pointers. Canonical `ax32xx_videoSetAddr` writes full pointers, confirming the
  distinction. New production-backend fixture test passed: **10 targeted
  DE/CSI/profile/hardware tests**. QEMU rebuilt. **Full suite and stock/Luma smoke
  tests have NOT been rerun after the latest DE changes.** Earlier full result
  was 367 passed, 5 skipped. Latest runner-only suite: 10 passed.
- LCD ready bit 31 had both states on hardware; emulator still forces it high,
  now marked as an approximation. Do not invent timing from snapshots.
- **Luma has NOT run on silicon.** Its board HAL still needs DMA/cache and panel
  latch validation. Measured contract for its display path: `DE_FRAME_CTRL`
  (`0xc800`) bit 1 is the double-buffer commit, it completes at a **frame
  boundary**, and it self-clears; `c800 == 1` is the correct steady state. So Luma
  must issue exactly one `deUpdate`-style commit (`c800 = 1|2`) after programming
  `c828`/`c830` at init, or the first framebuffer address never takes effect.
  Luma's `present()` writing 1 is not itself wrong. Luma's screen/video size
  `240x320` matches stock USB mode, but its `240/4 = 60`-word stride matches no
  observed stock stride (stock used `0x40` words = 256 bytes for a 240-wide plane).
  No RAM takeover/stager was launched.
- Preview capture is a **hardware rotation**: the SFR1 rotate engine
  (`0x898c..0x89a8`) turns the CSI `320x180` plane into the DE `180x320` plane,
  proven by its destination registers equalling the live DE planes. Direction is
  fixed in hardware (`ROTATECON` has no angle field). Luma does not need to use
  the engine, but must not assume the two planes are the same buffer.
- Working tree has substantial uncommitted changes. Review before committing.
  No subagents used. No additional hardware action is running in background.
- **Sensor bring-up now has its power and clock sequence (offline only).**
  `custom-firmware/include/ax_sensor.h` holds the shared IIC0/SCCB layer and
  `custom-firmware/include/ax_cam_power.h` the LDO/MCLK/PE10 mux, both transcribed
  from the stock routines (`ax32xx_sysLDOSet 0x0202b284`, `ax32xx_csiMclkSet
  0x0203444c`, `hal_iic0Init 0x02038730`). The board calls a bounded `sensor_detect()`.
- **Root cause of the bench `0xff` is settled, and it was our bug.** `sensorprobe`
  called `ax32xx_sysLDOSet` with index 6 and `en=0`. The stock LDO indices only
  reach 5, so index 6 hits the default branch and returns immediately: the imager
  was **never powered**. It was not a protocol mismatch and not a firmware
  mismatch. Correct sequence is `SYS_LDO_LSEN(1)` with `DVDD=6`, then
  `SYS_LDO_HSEN(0)` with `AVDD=16`.
- **H63P parameters recovered from RAM** (`_Sensor_Adpt_` at `0x020e32a0` in the
  baseline `ram1.bin`): **mclk 24 MHz**, DVDD 6, AVDD 16, VDDIO 16, **1280x720**,
  YUV, csi_tun 11. The ID check runs at 16 MHz (divisor 17); the sensor then runs
  at 24 MHz (divisor 11), where `div = 288000000/freq - 1` lands in `0x884c` bits 12:8.
  IIC0 runs at the stock 200 kHz (divisor 143).
- `tests/test_luma_sensor.py` covers all of it: the SCCB layer against the
  production emulator I2C model (including unpowered → definite **NACK**, not a
  bogus `0xff`), and the LDO/MCLK bit fields including a regression for the exact
  index-6 no-op that caused the bench failure. Luma builds (55,072 B) and all
  three smokes pass.
- **Luma flash risk re-audited on the newly built image.** No update/nor/erase
  symbols are linked, the stock JEDEC helper is absent from the flat image, and
  `print_photo`/`update_check`/`usb_storage` are NULL/unavailable. Luma still has
  not run on silicon.

## Luma runs from RAM on the original KC02 (2026-10-15, late)

**Luma has executed on the camera from RAM**, twice over, with no flash write at
any point. This is the milestone the whole hardware effort was building toward.

- **Bounded run.** `docs/hw/show_luma.py` asks the stock allocator for a block,
  links the bench image for the address it returns, uploads it with readback,
  runs Luma's real `hardware_init` + `app_render` + `present` for one frame, then
  returns. Evidence: `~/kc02-hw/2026-10-15/session-202910/final-bounded-230347`
  (beacon `LUMB`, `de_video_y` == Luma's own `lcd_y`, panel 240x320, frame
  captured and converted to PNG showing Luma's boot screen).
- **Full takeover.** `show_luma.py --takeover` jumps to Luma's real `_start`,
  which sets `PICMR = 0` and takes the machine over. The camera panel then shows
  **Luma's UI, confirmed visually by the user**. Recovery is a pinhole reset.

### Why memory comes from the allocator

Putting the image in the firmware's *free* pool and snapshotting it for restore
cannot work: the running firmware starts handing that memory out, so the snapshot
stops describing live state. The observed result was a corrupted heap and a hung
firmware. `ramalloc.c` now asks the stock allocator for a block and keeps it, so
the block is exclusively ours and nothing needs restoring. `run_bounded()` also
restores on the failure path, not only on success.

### Display bugs found only by hardware validation

Luma assumed it owned the display from reset. It does not: it runs *after* the
boot chain. Five defects, all fixed, all recorded in `docs/hw/2026-10-15.md`:

1. **No LCD frame kick.** `ax32xx_lcdKick()` is exactly `LCD_CON(0xa800) |= 2`,
   raised by the stock LCD interrupt every frame. A polling app must kick it
   itself. Without it a DE commit is frame-gated on a boundary that never comes.
2. **Assignments instead of read-modify-write.** Stock `LCD_CON = 0x31` and
   `DE_DISPLAY_CTRL = 0x2ce52083`; Luma wrote `1` to both, clearing fields it
   does not model and turning the panel path off.
3. **Wrong plane pitch.** The DE video plane is 240x320 with a **256-byte** pitch
   (`0xc82c = 0x40` words), not 240.
4. **Storage before the first frame.** `sd_hw_init` ran before rendering; a first
   frame that waits on storage never appears when storage misbehaves.
5. **Zeroed chroma is not neutral.** Both `sensor_copy()` and the DE subtract 128,
   so all-zero U/V renders as saturated **green**. This was the green rectangle in
   the viewport and the green garbage at the plane edge, where the 16 padding
   columns of a 256-pitch plane are never written by `present()`. Every plane is
   now filled neutral at init. Verified numerically: padding columns read exactly
   128 and the whole plane has zero green pixels.

Also fixed: the rotate engine and the CSI-to-LCD DMA, which the stock firmware
leaves writing the DE plane, are now disabled so Luma owns the plane outright.

### Button input is fixed and verified

The UI looked dead to input while it was demonstrably rendering. Cause, taken
from the stock firmware rather than guessed: `hal_adcGetChannel` (0x0203814c)
calls `ax32xx_adcEnable`, `ax32xx_adcSetBaudrate(1000000)`, `ax32xx_adcConverStart`
and `ax32xx_adcRead`, and Luma did only the middle-last of those. Specifically it
never enabled the **ADC module clock** -- `ax32xx_sysClkSet(29,1)` clears bit 29
of `0x8840`, and a *clear* gate bit is the enabled state -- and never programmed
the **baud divider** (`0x9174 = 144000000/(baud*2)-1 = 71`). With the module
clock gated off the conversion never completed, so every keypad read timed out and
returned "no key". `key_sample()` now follows the stock sequence byte for byte.

**Verified on hardware**, not just derived: with the user holding OK (upper
right), a bench run sampled the keypad ladder 32 times and read **388..392**
against a measured OK centre of **386** and window 351..421
(`~/kc02-hw/2026-10-15/session-202910/keypad-held-231023`). Luma maps 386 to
`KEY_OK` within a tolerance of 25, so the press registers. Idle reads ~1023.

### Also fixed for the panel

Frame events are now acknowledged as well as kicked. The stock firmware clears
every `LCDSTA(0xa804)` event bit by writing it eight positions higher, from its
LCD interrupt. A polling application that only kicks (`LCD_CON |= 2`) without
acknowledging stops getting frame boundaries, and because a DE commit is
frame-gated the panel then freezes on the last frame it latched -- observed as
Luma sitting on its boot screen with the progress bar never advancing while the
code was running. `lcd_service()` now does both.

### Still open

- The sensor does not answer on either IIC bus in USB mode, so Luma's camera view
  shows a neutral field rather than an image. See the sensor sections below.
- Panel orientation: the captured plane reads rotated relative to how it is drawn.
  Whether the panel's own scan makes that correct has not been confirmed, and the
  user has not reported it as wrong.
- A faint dark (not green) edge artifact remains at the plane padding columns.

## Flash preparation

`custom-firmware/tools/image.py` packs a LUMAIMG container matching
`image_validate()` in `update.c` field for field. `tests/test_image_pack.py`
compiles that production validator and runs it over the packer's output, so the
two cannot drift: the container validates, and each of 14 tampered fields
(payload byte, flags, entry, board mask, version, load address, reserved byte,
both CRCs) is rejected.

`flash_qualified`, `recovery_writer` and `hardware_tested` stay false, install
filenames are refused, and the safe first install remains the **bringup** image,
not Luma: no external SPI read has been verified and the recovery gate has never
run on this hardware.

## Safety record

Zero flash writes across the entire effort. The RAM-run write allowlist was
tested and refuses every address outside the allocated block, including the stock
application image and flash. Every hang — including one real heap-corruption hang
and one wedged USB BOT state — recovered with a USB replug or the pinhole reset.

Read this before [`HANDOVER_EMULATOR.md`](HANDOVER_EMULATOR.md). That document
covers the earlier workstream (running the *unchanged stock* firmware in QEMU) and
its "CURRENT STATE" sections name a commit from months ago. Its emulator facts are
still good; its status is not. For the current state, this file wins.

- Repo: `/home/jbaiter/.src/kc02-fw-mod`, branch `reversing`, session-start HEAD `822c6c2`.
- **Hardware testing started 2026-10-15.** Stock USB, baseline RAM, bounded SPR,
  sensor, ADC, TT timer and JEDEC read-ID probes passed. RAM/peripheral changes
  only; no flash writes or erases.
- Working-tree changes include hardware-derived emulator fixes. Full pytest:
  **367 passed, 5 skipped** after the ADC/TT/JEDEC changes; two subsequent
  bench-guard tests also pass in the targeted runner suite.
- Evidence: `~/kc02-hw/2026-10-15/session-202910/` and
  [`docs/hw/2026-10-15.md`](docs/hw/2026-10-15.md).

---

## 1. Scope decision (settled)

**Hardware work targets the original KC02 only.** The X8D and Z-12 build and are
covered by emulator tests; they are not release targets and no hardware effort
goes into them.

Two things follow, and both relax constraints that were tight:

| | Original KC02 | Z-12 (no longer binding) |
|---|---|---|
| Application RAM (`0x02000000` up) | **7 MiB** (`stack top 0x027ffffc`) | 1 MiB |
| Largest erased, sector-aligned flash window | **992 KiB** | 264 KiB |

So memory is no longer scarce, and the flash layout is a single constant rather
than a cross-board minimum. The remaining Z-12 value is regression coverage: its
sensor switch and the X8D key ladder both caught real bugs in the shared core.

---

## 2. The two workstreams

| | Stock emulation | Luma |
|---|---|---|
| What | Run the unchanged vendor firmware in QEMU | New freestanding firmware |
| Docs | `HANDOVER_EMULATOR.md`, `docs/EMULATOR_PLAN.md` | `custom-firmware/{README,DESIGN,BRINGUP}.md` |
| Policy | Never patch guest instructions or inject application state | Our own code, own HAL |
| Status | Complete for the original, alternates, capture, printing | RAM milestone plus service cores |

`custom-firmware/` is the new tree. The emulator lives in `tools/emu/` plus the
machine sources it owns in `tools/emu/qemu/ax3292/`.

---

## 3. What exists and is verified

**Luma** (`custom-firmware/src/ui.c` and friends): three screens (camera, gallery,
list settings), RGB565 320×240, antialiased text and geometry, boot/shutter
animations, SD-loaded theme (colours plus two BMPs), capture to BMP, gallery,
shared no-print/dither/grayscale policy. Builds for three boards from one core,
bounded by the *original's* 7 MiB now that Z-12 is out of scope.

**First-stage bring-up** (`custom-firmware/bringup/`): a recovery gate and a GDB
monitor that run before any UI, entering at the factory application entry
(`0x0200046C`). Preserves the BLDR header, boot code, SDRAM initializer and
resources. `tools/factory.py` refuses to change a byte outside the application
copy span, and refuses the install filenames.

**The monitor**: trap-based breakpoints and single-step. Own vector table at a
fixed `EVBA`, 16-byte slots, full context save. `src/or1k_decode.c` decodes
control flow, so a step traps the *successors* of the current instruction.

**USB**: three interfaces on one composite device.

| Function | Interface | Class | Endpoints |
|---|---|---|---|
| Mass storage | 0 | 08/06/50 | EP1 IN/OUT |
| Vendor debug (GDB) | 1 | ff/4c/01 | EP2 IN/OUT |
| WebUSB update | 2 | ff/55/01 | EP3 IN/OUT |

Mass storage speaks BOT plus a SCSI subset; the LUN reports **no medium** until
`sd_handoff` gives the card to the host. WebUSB carries a staged container for
**both firmware and themes**.

**Update paths**: `src/update.c` (validated A/B engine), `src/update_sd.c`
(`FIRMWARE.IMG` discovery, `READY` vs `OK`), `src/webusb.c` (staged container,
per-file CRCs), `src/theme_bundle.c` (theme validated as a set, committed
images-first).

### Verification, and what each kind proves

- `tests/test_custom_firmware.py`: real C core compiled with UBSan.
- `tests/test_msc.py`, `tests/test_webusb.py`, `tests/test_update_sd.py`,
  `tests/test_theme_update.py`: protocol peers that act as the USB host, over
  real FAT16 card images. These are conversations, not calls into my own helpers.
- `custom-firmware/tools/smoke.py`: Luma in QEMU on all three profiles.
- `custom-firmware/tools/bringup_smoke.py`: relocation, checkpoint cycles, and
  the in-guest trap/step self-test, which reports `failures: 0x0`.

Evidence lives under `~/.cache/kc02-emulator/`, notably `luma-aa-final`,
`msc-selftest2`, `msc-update3`, `luma-after-webusb`.

---

## 4. Not done, and not claimed

1. **No cold boot.** The stage reuses the factory clock, SDRAM and pin
   configuration. Nothing brings silicon up from reset.
2. **No NOR driver.** The update engine validates and refuses; it has never
   written flash.
3. **Nothing has enumerated.** QEMU does not model the USB controller. Two and
   three independent bulk endpoint pairs is an SDK assumption; the stock firmware
   demonstrates only EP0 and EP1.
4. **Luma itself has not run on hardware.** The stock-firmware RAM probe path
   works; custom USB enumeration, panel/sensor initialization and printing still
   need hardware qualification.
5. Cache maintenance for trap words is written to the vendor contract but
   unverified. HP shadow-bank capture is absent. Ctrl-C cannot halt a running
   target. The boot-prefix comparison has no trusted snapshot, so the recovery
   gate stops at `RECOVER_PREFIX_UNAVAILABLE` (code 6).

---

## 5. Measured facts from the last session (do not re-derive)

These cost real effort. They are measurements, not documentation.

### 2026-10-15 bench results and emulator corrections

Full values, commands and caveats: [`docs/hw/2026-10-15.md`](docs/hw/2026-10-15.md).
Portable fixture: `tests/fixtures/hardware/original-2026-10-15.json`.

- JEDEC ID **`5e4016`**, read twice through the pinned stock `0x9f` helper.
  The old `ef4016` was an emulator assumption. Corrected the original profile
  and machine default; alternate identities remain unchanged. UID unmeasured.
- Keypad ADC channel 3 returned `1023`; `ADCCON` read `0x13` before/after a
  stock HAL conversion. GO self-clears along with BUSY. Corrected the emulator.
- TT (`9900/04/08`) is disabled at the stock USB handoff. Start `0x42` reads
  back as `2`; stop `0`/`0x40` returns control/count zero. A 50-XOS-ms sample
  measured about 128 MHz, **not microseconds**. Added a gated/resettable TT model.
  Luma now starts it and extends raw 32-bit ticks before converting to ms.
- DE geometry `013f00ef` confirms 240×320 portrait. Y/UV stride registers `40`
  confirm 256-byte pitch. Existing emulator decoding agrees. CSI LCD DMA is
  idle in USB mode; camera-preview configuration is still pending.
- Stock sensor autodetection selected real H63P ops `020e32a0`; independent
  helper read still returned `ff` and remains inconclusive. Added an original
  H63P identity regression. ADC/SD divider readback semantics remain unresolved.
- Fixed three stale offline unlock self-test expectations before USB access.
  `camprobe2` probes a mailbox, not SPRs, and its default input implied a
  multi-day wait. The runner now refuses those defaults.

Update the emulator fixture/tests in the same session as each finding. Change
confirmed mismatches; keep state-specific observations out of reset defaults.

### Flash layout per board

| Board | Last non-`0xFF` byte | Largest erased aligned window |
|---|---|---|
| original | `0x3071FF` | `0x308000`–`0x400000` = **992 KiB** |
| X8D | `0x1CEFFF` | `0x1CF000`–`0x400000` = 2244 KiB |
| Z-12 | `0x3FFEFF` | `0x3BD000`–`0x3FF000` = 264 KiB |

`0x307200` is **not** sector-aligned, so erasing the sector containing it would
also erase the last 512 bytes of assets at `0x307000`–`0x3071FF`. Always align
the window up from the asset end, and confirm the erase size on hardware by
erasing one candidate sector and checking `0x307000`–`0x3071FF` is unchanged.

### Nothing validates the application payload

- No CRC32 polynomial appears anywhere in the boot or app region of the dump.
- The header's additive checksum covers only the first 512 bytes (it sums to 0).
- The validated surface is: `BLDR` magic, `0x55AA`, the two `0x01234567` markers,
  `SFAT` at `0x304`, and that checksum. The updater's checks are the same.
- The SDK boot source's copy is a dumb sector loop with no checksum
  (`flash_to_psram` in `ax32xx/src/ax32xx_int.c`).

**Two consequences.** A tiny loader in the app region's first sectors boots with
anything after it, which makes a recovery-first trampoline viable. And an
interrupted write to the app region is undetectable: the boot chain copies
garbage and jumps into it.

Caveat: that is the SDK's source. The repo matched 60 boot symbols with high
shape fidelity, but constants differ per board. Strong evidence, not proof for
our dump. Settling it means diffing the dump's copy loop against the vendor's
`ax32_platform_demo/output/ax329x_sdk.lst`.

### The SFAT table (original)

At the documented `0x0D3200`, 97 monotone 8-byte `{u32 offset, u32 size}` entries,
highest asset end `0x231D69`. Guessing the offset from the app end finds garbage;
use the documented address.

### The emulator cannot answer boot-chain questions

`tools/emu/qemu/ax3292/ax3292.c` copies only the *boot region* into low RAM
(`flash[0x200:]`, capped at 64 KiB) and loads the application through `-kernel`.
The BLDR chain is **modelled, not executed**. Anything about header validation,
the copy span or the handoff has to be answered from the dump or on hardware.

---

## 6. Hardware plan (original KC02)

Ordered. Each step gates the next.

### Step 1 — RAM-only peripheral readback (read-only, no flash)

The stock firmware boots and *is* the bring-up. A payload runs inside it from a
measured-free RAM window, using the firmware's own USB and I-cache helper.

```sh
.env/bin/python tools/kc02_usb.py selftest        # offline guard check
lsusb | grep -Ei '0219|1908'                       # identify the USB mode
# SESSION must name a new persistent evidence directory; see the dated reference.
.env/bin/python docs/hw/build_probes.py --output "$SESSION/probes"
.env/bin/python docs/hw/execute_payload.py --payload sprprobe --input-words 0 \
  --payload-dir "$SESSION/probes" --output-dir "$SESSION/spr-de"
```

Purpose: replace my QEMU-derived register guesses with measurements. Read back
the DE geometry and strides, the panel registers, the ADC channel and thresholds,
the CSI setup, the SD divider, and the flash ID. Current bounded probes:
`echo`, `sprprobe` (five fixed register banks), `sensorprobe`, `adcprobe`,
`timerprobe`, `flashidprobe`. Historical `mode`/`camprobe`/`camprobe2` payloads
change or observe UI state; they are not register-readback substitutes.

Known risk: `tools/kc02_usb.py probe` (the `BLDR` at `0x01FFDA04` flash-window
test) has a documented bus-error risk and may need a power-cycle. The SPR payload
only reads peripheral registers; its transport writes scratch RAM and runs cache
helpers. Sensor/ADC/TT probes make bounded, reversible peripheral changes. None
writes flash. See the dated bench reference.

### Step 2 — flash driver test in the tail (writes, but reversible by the device)

The 992 KiB window is erased and unread. Program a sector, verify it, erase it
back to `0xFF`, verify that, then diff the whole 4 MiB against the dump. Even a
power failure mid-test is harmless.

Six guards, all cheap:

1. **Prove the mapping before writing.** Read the BLDR header, the app start and
   the SFAT marker, and compare against the known dump. Any disagreement means
   the addressing is wrong: refuse.
2. **One guarded write primitive.** Every erase and program goes through one
   function taking `(offset, size)` that refuses anything outside a window
   computed from verified readback. The dangerous thing should be inexpressible.
3. **Erase-size arithmetic from the chip**, discovered by erasing one candidate
   sector and confirming the last 512 asset bytes are untouched.
4. **Erase only what reads `0xFF` on entry; restore to `0xFF` on exit.** The test
   is idempotent, so re-running after a crash is safe.
5. **Full-flash diff afterwards**, outside the declared window. A difference is a
   loud failure.
6. **Run it under the stock firmware** via the RAM payload path, so a hang is a
   power-cycle.

Plus: fresh power, no auto-power-off, card unmounted, and print the intended
range before touching anything.

### Step 3 — SPI recovery (do this before any irreversible write)

The user's CH341A path currently bugs out from interference; lifting the chip is
reserved for later. **This is not required for steps 1 and 2**, which is exactly
why they are ordered first.

### Step 4 — recovery-first trampoline, then install

Real firmware lives in the reversible tail; the app region holds only a small
loader that validates the tail payload's CRC, boots it, and otherwise exposes
recovery. That makes the install the *only* irreversible write, as small as one
or two sectors, and every later update tail-only and therefore reversible.

**Hard rule: the app region stays hard-refused in the driver until step 3 is
done.** No exceptions, because nothing in the boot chain would notice a
half-written application.

---

## 7. Reference values needed on the bench

Every board's `entry`, `stack-top`, `vbg-addr`, key/ADC ladder, panel rotation and
sensor are in `tools/emu/profiles.py` (`Profile.options()`, `machine_args()`).

Original KC02: entry `0x0200046C`, stack `0x027ffffc`, VBG `0x222c`,
panel 90°, ADC channel 3, H63P sensor at slave `0x40`, ID `0x0b == 0x48`.
Keys `(657, 510, 252, 776, 386, 12)` = Up, Down, Left, Right, OK, Escape.
Extras `(386, 1024, 1024, 1024, 1024)`. Flash **`5e4016` measured**;
UID unmeasured (the emulator still assumes zeros).
Printer platen phases `(10, 8, 9, 1, 5, 4, 6, 2)`.

Register contracts:

- ADC `0x9170` CON, `0x9174` BAUD, `0x9178` DAT.
- SD `0x9880` status, `0x9884` control, `0x9888` divider, `0x988c` R2 buffer,
  `0x9890/94` DMA addr/len, `0x9898` cmd, `0x989c` argument/response.
- DE `0xc800` frame, `0xc810` geometry, `0xc818` enable, `0xc828/2c` Y + stride,
  `0xc830/34` UV + stride.
- CSI LCD `0xb060/64` Y/UV, `0xb068` stride, `0xb180` size, `0xb008` bit19 latch.
- JPEG in `0xb820/24` for ctrl `0x400`, else `0xb050/54`; JPGA IRQ16, JPGB IRQ22.
- Printer: supply/job `0x8004` bit6, latch bit5; clock `0x800c` bit13, data bit15;
  strobe `0x8000` bit9; PA7 paper active-low.
- Timer0 `0x9800/04/08`, pending `0x98a8`; TT `0x9900/04/08` is a
  start/stop/resettable raw counter, approximately 128 MHz against stock XOS
  time, disabled in USB idle. **`0x9908` is not a free-running µs clock.**
- `sd_hw_init` returns the vendor CSD value, which is numerically the card's
  512-byte sector count, not a byte count.

---

## 8. Traps that already cost time (do not re-learn)

1. **ctypes callback lifetime.** C stores the function pointer; Python may
   collect the callback and the next call jumps into freed memory. Every test
   that mounts a card keeps its readers alive in a module-level list. This bit me
   three times, in three different files.
2. **Two byte orders.** `m`/`M` carry memory bytes in address order; `p`/`P` carry
   register values little-endian. Comparing a register read against the numeric
   hex of its value makes a working target look broken.
3. **SCSI CDB fields are big-endian; BOT headers are little-endian.** Mixed, every
   transfer lands at the wrong block.
4. **The composite configuration descriptor is 78 bytes**, so EP0 must stream it
   in two packets. Anything over 64 bytes needs the multi-packet path.
5. **The staged container's entry is 20 bytes.** At 16 the CRC overlaps the last
   three bytes of the 8.3 name and corrupts every extension.
6. **WebUSB details**: `iLandingPage` is `wValue` and `GET_URL` is `wIndex`; a BOS
   needs `bcdUSB` 0x0210; the capability UUID is little-endian.
7. **gcc 4.9 rejects `(Struct){0}`** under `-Wmissing-field-initializers`. Use a
   `static const` zero instance.
8. **The test harness's DMA address must resolve to real bytes.** A token that
   does not resolve tests the wrong buffer and agrees with itself.
9. **`crc32` lives in `src/crc.c`**, not in the theme parser, so a checksum does
   not drag the theme engine in.
10. **The Luma linker script keeps the 2 MiB bound** even though Z-12 is no longer
    a target. It costs nothing and keeps all three building. Relax it only
    deliberately.

---

## 9. Commands

```sh
# Build
python3 custom-firmware/tools/build.py                     # Luma, 3 boards
python3 custom-firmware/tools/build_bringup.py             # original, USB detached
python3 custom-firmware/tools/build_bringup.py --selftest \
  --output custom-firmware/build/bringup-selftest
python3 custom-firmware/tools/build_bringup.py --usb-vid 0x1209 --usb-pid 0x0001 \
  --output custom-firmware/build/bringup-usb

# Test
python3 -m pytest -q                                        # 362 pass

# Luma in QEMU (needs a private create-only card)
python3 custom-firmware/tools/card.py ~/.cache/kc02-emulator/luma.img --demo-photos
python3 custom-firmware/tools/run.py --board original \
  --sd-card ~/.cache/kc02-emulator/luma.img
python3 custom-firmware/tools/smoke.py --output ~/.cache/kc02-emulator/luma-NEW

# First-stage QEMU checks
python3 custom-firmware/tools/bringup_smoke.py --output ~/.cache/kc02-emulator/bu-NEW
python3 custom-firmware/tools/bringup_smoke.py --build custom-firmware/build/bringup-original \
  --card <card with FIRMWARE.IMG> --expect-update-version N \
  --output ~/.cache/kc02-emulator/bu-card-NEW

# Stock emulator regression
python3 tools/emu/smoke.py firmware/original.bin --output ~/.cache/kc02-emulator/stock-NEW

# Hardware (no flash writes; prepare SESSION as in docs/hw/2026-10-15.md)
.env/bin/python tools/kc02_usb.py selftest
.env/bin/python tools/kc02_usb.py dump-ram --out "$SESSION/ram1.bin"
.env/bin/python docs/hw/build_probes.py --output "$SESSION/probes"
.env/bin/python docs/hw/execute_payload.py --payload sprprobe --input-words 0 \
  --payload-dir "$SESSION/probes" --output-dir "$SESSION/spr-de"
# Do not run the old flash-window probe as a routine bench step.
```

Toolchain: working patched little-endian/no-delay compiler at
`~/.cache/kc02-toolchain/prefix/bin/or1k-elf-`. Do **not** use
`~/.src/ax3292-toolchain/out/prebuilt-or1k-elf/` (unpatched, big-endian).
QEMU: `~/.src/qemu-11.1.2/build/qemu-system-or1k`.
GDB: `~/.cache/kc02-gdb/root/usr/bin/gdb-multiarch`.

---

## 10. Immediately next

USB-idle baseline and bounded probes are complete. No mode switch or flash
write/erase has run. Evidence: `~/kc02-hw/2026-10-15/session-202910/`.
**20:51:54+02:00: camera absent from USB.** Preview preflight stopped before
applying the hoist; user reconnection/power-on is required.

Next: confirm the visible camera screen and stable battery power, install only
the RAM USB hoist and request camera mode, then re-read DE/CSI banks in preview.
Keep those measurements separate from USB idle.

The runner now caps code at `020d9000` to reserve the unlock, hoist and child-lock
stubs below `020d9700`. The generic toolchain window is wider; do not use it to
upload over active stubs. Update emulator evidence/tests after each finding.

The flash-window `probe` remains deferred; earlier hardware notes already
recorded a bus fault there. Step 2 flash-driver work has not started.

Save everything to `~/kc02-hw/$(date +%F)/` and record the commands alongside the
outputs. Write the findings back into this file's section 5 rather than only into
the chat, because that section is what the next session reads.
