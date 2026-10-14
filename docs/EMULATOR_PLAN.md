# Emulator plan — Stage 3 (app boot + Display + SPI flash)

Goal: run a dumped (or patched) AX3292 firmware image on this laptop far enough to
render its UI, so patches can be tested before anything is flashed. Priorities,
in order: **SPI flash**, **display**, then the minimum else needed to make the UI
actually run (timer/IRQ, keypad).

Non-goals for Stage 3: USB device emulation, camera/ISP capture, printer physics,
cycle accuracy, and boot-ROM fidelity. Camera/sensor/printer are hard stubs.

## What the repo already gives us

| Need | Where |
|---|---|
| ISA: OpenRISC 1000, LE, no delay slots | `docs/BOOT_MAP.md`, Ghidra `KC02:LE:32:nodelay`, reconstructed `or1k-elf` toolchain |
| Complete SPR/register map | SDK `ax32xx/inc/ax329x.h` (1089 SFR names) + `docs/BOARD_RE.md` port bits |
| SPR address scheme | `SPR = (group << 11) | (reg << 2)` (`MAX_SPRS_PER_GRP_BITS = 11`) |
| Memory map | `docs/RAM_MAP_MEASURED.md`, `docs/BOOT_MAP.md` |
| SPI flash protocol | SDK `ax32xx/src/ax32xx_spi.c`, `hal/src/hal_spi.c` |
| Display pipeline | SDK `hal/src/hal_lcd.c`, `hal_lcdframe.c`, `ax32xx_lcd.h` |
| Screen/UI layout (oracle) | `docs/camera_screen/`, `docs/main_menu/` host models |
| Device/UI maps | `docs/FIRMWARE_MAP.md`, `docs/UI_STATE_MACHINE.md`, `docs/BOARD_RE.md` |

Register map usable directly from the SDK header, e.g.:

```
PORTA  0x8000  PORTB  0x8004  PORTE  0x800C  PORTF 0x8010
PICMR  0x4800
SPIFACT 0x9244  SPIFSTA 0x9248  SPIFBAUD 0x924C  SPIFDBUF 0x9250
SPIFDCNT 0x9254  SPIFBADR 0x9258  SPIFSADR 0x925C  SPIFCRC 0x9260
LCDCTRL 0xA800  LCDSTA 0xA804  ...                (SFR5, OSD/video block)
LCD layer regs seen in firmware: 0x8900..0x891C    (SFR1; resolve which block)
```

## Architecture

```
        host runner (Python)
        ├── framebuffer -> PNG / live window
        ├── SPR & MMIO trace (names from ax329x.h)
        ├── key injection, scripted run
        └── optional GDB stub
                 │
        ┌────────▼────────┐
        │   or1k CPU core │
        └────────┬────────┘
             bus │ SPR dispatch
        ┌────────┴─────────────────────────────┐
        │ RAM: 0x0..0x02800000 (SRAM+SDRAM)    │
        │ SPI flash device  (dump-backed)      │
        │ Display/LCD device (YUV -> RGB)      │
        │ Timer/IRQ, GPIO, ADC, I2C, RTC, USB  │
        └──────────────────────────────────────┘
```

### CPU core — decide in a spike (M0)

* **Preferred: QEMU `qemu-system-or1k`** (or1200 TCG, fast, gdbstub) with a small
  custom AX3292 machine, or a hook that routes non-architectural SPR accesses to
  the device models. The peripheral space is SPR-based, so the crux is whether
  QEMU's `target/openrisc` cleanly exposes arbitrary SPR reads/writes to a
  machine callback. If it does, this is by far the least work and gives TCG speed
  for a live UI.
* **Fallback: custom interpreter** (C, ~1–2k lines) with an explicit SPR/MMIO
  dispatch table. Full control and trivial Python integration, but slower and it
  is a CPU core to get right (the variant is no-delay-slot, which simplifies it).
* **Prototyping only: Ghidra P-code emulator** (`EmulatorHelper`), for
  function-level tests before the system emulator exists. Fast to stand up; not
  fast enough for a live UI.
* Ruled out: Unicorn (no OpenRISC backend); Renode (OpenRISC support
  unconfirmed — verify before relying on it).

**M0 result — answered from source.** QEMU exposes *no* hook, so the patch is
required, and it is small:

* `target/or1k/sys_helper.c` implements a fixed set of architectural SPRs.
  Anything else is **silently ignored**: `mtspr` drops the write and `mfspr`
  returns the destination register's previous value. No trap, no callback. The
  decode is in `target/or1k/translate.c` (`trans_l_mfspr`/`trans_l_mtspr`,
  `SPR = rA | K`).
* QEMU's group-0 numbering is the OpenRISC spec, which this chip does not use:
  QEMU `SR = 0x11`, chip `SR = 0x18`; QEMU `NPC = 0x10`, chip `0x14`; QEMU
  `PICSR = 0x4802`, chip `0x4808`. EPCR (`0x20`) and ESR (`0x40`) happen to
  agree. So the firmware's interrupt enable/disable writes would be dropped,
  and the chip's `NPC` at `0x14` collides with QEMU's `FPCSR`.
* `qemu-system-or1k` ships `or1k-sim` (DRAM at 0, UART at `0x90000000`) and
  `virt`; a 64 MiB `-m` covers the firmware's SDRAM at `0x02000000`.

The patch therefore does two things: (1) give the CPU an SPR-bus hook the
machine services **before** the built-in switch, and (2) implement a machine
that maps the AX3292 numbers onto QEMU's CPU state (`0x18`→SR, `0x20`→EPCR,
`0x40`→ESR, `0x14`→NPC, `0x1C`→PPC, `0x50`→EVBAR, shadow GPRs `0x100`+) and the
SFR groups (≥ `0x8000`) onto device models. Numbers and names come from
`tools/emu/sfr_map.py`.

Useful scaffolding already exists: `tools/emu/sfr_map.py` + `sfr_map.tsv`
(1065 SPR numbers with names and confidence) and `tools/emu/spr_census.py`, a
static census of the firmware's SPR accesses that ranks the device backlog.

**M0.5 result — it boots.** `qemu-system-or1k` now builds with a new `ax3292`
machine (patch and build script under `tools/emu/qemu/`) and runs the stock image
into the firmware's HAL. Four upstream-versus-device differences had to be fixed,
and each was established from the image rather than assumed:

| # | Upstream QEMU | AX3292 |
|---|---|---|
| 1 | branches have delay slots | none — 55% of branches in `original.bin` are immediately followed by another branch (878 `l.jal`→`l.jal`) |
| 2 | hardcoded big-endian | little-endian (reset `SR` has `LEE`) |
| 3 | unknown SPRs silently ignored, no hook | every peripheral is an SPR |
| 4 | OpenRISC spec group-0 numbering | `SR 0x18`, `NPC 0x14`, `PPC 0x1C`, `EPCR 0x20`, `ESR 0x40`, `EVBA 0x50`, `VR 0x00` |

The boot code (file `0x520`) sets SP to `0x027FFFFC` and calls `0x0200046C`;
that is the application entry the machine now jumps to. The trace then shows the
HAL initialising clocks and LDO (`0x8840`/`0x8868`), the RTC (`0x88E0`/`0x88EC`),
cache control (`0x1800`/`0x2000`) and the interrupt controller
(`0x4800`/`0x4814`), before writing one byte to UART0 (`0x9088`) and settling
into a poll loop on the UART pending register (`0x908C`) — 39,871 of the first
40,000 accesses.

So the first device to model is neither the SPI flash nor the LCD: it is the
**UART**, which the firmware talks to before anything else. With it modelled the
boot should continue, and the same trace-driven loop then builds the rest of the
peripherals on demand — which is the plan's SPI flash and display work.

**M0.6 result — the whole early boot runs.** Driven by the trace, four more
things went in: UART0, the SPI0 flash controller, the LCD register block, and a
generic read-back store for the remaining SFR registers (the GPIO and LCD code
does read-modify-write, so returning zero is not good enough). The stock image
now:

* prints its boot log — `sdram *saddr=0x2103980,*eaddr=0x27fec00`, `RC128k=0x0`;
* reads the flash JEDEC ID (`0x9f`) and then the asset tables, byte mode first
  and then by DMA;
* initialises clocks, LDO, RTC, DAC, ADC and GPIO;
* runs the LCD panel init (`LCDCTRL`, `LCDSTA`, 130 `LCDCMD` writes);
* and lands in the RTOS idle loop at `0x0200c64c` (`delay(50); goto 0x0200c64c`).

Two details were worth the time it took to find them, and both are the kind of
thing that silently corrupts a flash read rather than failing loudly:

* Chip select is `SPIFGCON` bit 14, and the byte helpers rewrite that register
  before *every* byte, so the command state machine may only restart on a CS
  **edge** — resetting on every write turned the address bytes into commands.
* The vendor SDK sets `SPIFSADR = 0x6000000|0`: the DMA **continues** the
  address the byte stream already shifted in, so the controller's address field
  is the low 25 bits and `SPIFSADR` matters only for a standalone DMA.

The firmware now sits in its idle loop, which only the timer interrupt can
break out of. That is the next step, followed by the interrupt controller, the
framebuffer dump and the keypad.

**M0.7 result — the interrupt path, and why it needs the boot ROM.** The timer
and PIC are modelled (IRQ0, the bit the firmware enables, is raised on a 10 ms
virtual tick) and the interrupt is delivered and serviced. Three more findings,
two of them about the emulator and one about the chip:

* **This chip has no OpenRISC vector table.** Upstream vectors to
  `exception << 8`. The AX3292 dispatches through twelve 8-byte stubs at VMA
  `0x174`..`0x1CC`, each loading an exception code into `r3` and jumping to a
  common entry at `0x1D4`; that calls the boot section's dispatcher at `0x1BB4`,
  which jumps on to `0x1720`. So `vect = EVBAR + exception * 8`, and the machine
  sets `EVBAR = 0x16C` at reset — which is what the boot ROM does before entering
  the application. (The repo's SLEIGH language had the generic or1k vectors
  removed for the same reason.)
* **Timers need `-icount`.** A tight idle loop executes as one chain of
  translation blocks, and QEMU only processes `QEMU_CLOCK_VIRTUAL` timers when it
  leaves that chain, so an ordinary run delivered exactly **one** interrupt and
  then stopped. `-icount shift=6` ties the virtual clock to the instruction count,
  checks timers at every block boundary, and makes runs deterministic.
* **BQL discipline.** Timer callbacks run with the big QEMU lock held; SPR helpers
  do not. `cpu_interrupt()` from a helper therefore needs `bql_lock()` (as QEMU's
  own PICMR code does) while the timer callback must not take it. Backwards in
  either direction asserts.

The interrupt is now delivered, but servicing it lands in the boot SRAM's
exception machinery — which the boot ROM initialises via `exception_init`. Starting
at the application entry skips that. So the next step is not another device: it is
to **start from the reset vector** and let the boot code do its job (SDRAM and
clock set-up, exception installation) before it calls the application at
`0x0200046C`, instead of entering the application directly.

RAM is flat: load the boot section at 0x0 if starting from reset, and the
application at 0x02000000 from the dump. SDRAM is just RAM (8 MiB); the initial
stack is `0x027FFFFC`; the HAL pool and `.bss` are ordinary RAM.

Entry strategy: prefer **load the app + jump to its entry** for the first
milestone (find the bootloader's jump target), and treat **boot from reset**
(which also exercises the SPI flash model and stubs SDRAM/PLL init) as a later
fidelity step.

## P1 — SPI flash (first)

Registers (SFR2): `SPIFACT`, `SPIFSTA`, `SPIFBAUD`, `SPIFDBUF`, `SPIFDCNT`,
`SPIFBADR`, `SPIFSADR`, `SPIFCRC`. From `ax32xx_spi.c`:

* byte mode: `SPIFDBUF = b; SPIFACT = 1; while ((SPIFSTA & 1) == 0); return SPIFDBUF;`
* DMA mode: `SPIFDCNT = len/4-1; SPIFBADR = buf; SPIFSADR = 0x6000000|off;`
  `SPIFACT = BIT(17); SPIFACT = BIT(1); while ((SPIFSTA & BIT(1)) == 0);`

Model:

* 4 MiB backing store = the firmware dump (per build).
* Command state machine over the byte interface: JEDEC ID `0x9F`, status `0x05`,
  read `0x03`, fast read `0x0B`, and the DMA path. Chip-select comes from its GPIO;
  derive the exact sequencing from `hal_spi.c` `hal_spiFlashRead`/`…ReadID`.
* Writes/erases go to a **copy-on-write overlay** so a test can run an "update"
  without touching the reference dump.

Acceptance:

* `hal_spiFlashReadID` returns the real JEDEC ID.
* Reading a known asset through the firmware's own loader returns bytes identical
  to the dump (e.g. frame JPEG at 0x0D3510, menu background, a sensor table).
* A DMA read of a known range matches the dump.

## P2 — Display (second)

The pipeline is a `disp_frame_t` with `y_addr`, `uv_addr`, `stride`, `w`, `h`,
`scan_mode`, `posX/posY`, driven through `ax32xx_videoSetAddr/SetStride/
SetPosition/SetScanMode` and `ax32xx_osdSetUpscaler` (SDK `hal_lcd.c`,
`hal_lcdframe.c`). Layers: `VCH_TOP` / `VCH_BOT`. So frames are **planar YUV**,
not RGB; the 320×240 `0x12C00` figure is one plane.

Tasks:

1. Resolve the LCD register block(s): the SDK's `LCDCTRL` is SFR5 (`0xA800…`), but
   the firmware's driver writes layer registers around `0x8900` (SFR1). Read
   `ax32xx_lcd.h` + the firmware's LCD driver (`docs/CAMERA_SCREEN.md`,
   `docs/APP_MAP.md`) and pin down which block drives the panel and what each
   layer register means.
2. In the device model, capture layer source (`y_addr`/`uv_addr`, `stride`,
   `w`/`h`) and destination (`posX/posY`, upscaler) on register writes.
3. On frame completion (or periodically), read the Y and UV planes, convert
   YUV→RGB (resolve 4:2:0 vs 4:2:2 from `hal_lcdframe.c`), composite the visible
   layer(s), and write a PNG. Optional: a live window.
4. Model the LCD completion/status bits the firmware polls (the repo noted
   `SPR 0x8910` bit 1 and `SPR 0xA048` bit 4) so draw calls return.

Acceptance:

* The first frame dumped after boot is a legible screen.
* The main-menu and camera-screen renders match a photo of the device screen
  (and can be diffed against the existing host screen models).

## P3 — Timer / IRQ (minimum for a live UI)

The system tick (~10 ms) drives `system_tick 0x0200D2C0` and the key poll
(`docs/APP_MAP.md`, `docs/BOARD_RE.md`). Timer register block in
`ax32xx_timer.h`/`tminf`; interrupt controller `PICMR` at `0x4800` (PIC group) and
`SPR_SR` `0x18`.

* A host-side virtual clock ticks the timer at the right rate and raises the
  firmware's timer IRQ; model just enough PIC to deliver it and to read back the
  mask (the printer brackets `PICMR`).
* Use a **virtual clock**, not wall-clock, so the UI is deterministic and
  independent of host speed.

Acceptance: `system_tick` runs, the UI queue is pumped, and modes advance.

## P4 — GPIO + ADC keypad

* SFR0 ports (`PORTA/B/E/F` at `0x8000/4/C/10`): reads/writes for LED, USB sense,
  paper/cover, printer signals (printer can be a no-op). Model the bits the app
  reads during normal UI use; log the rest.
* ADC channel 3 is the keypad; the windows are known
  (`docs/BOARD_RE.md`: OK 386±0x23, RIGHT 776, LEFT 252, UP 657, DOWN 510,
  POWER 12). Injecting a key means presenting the matching ADC value; the
  firmware's own scan/debounce then produces the key event.
* Acceptance: inject UP/DOWN/OK and watch the UI change.

## P5 — Stubs

* I²C sensor: answer the H63P probe (`slave 0x40`, reg `0x0B → 0x48`) so bring-up
  succeeds; otherwise no-op.
* CSI/ISP: no-op (the camera path is out of scope).
* RTC: monotonic counter from the virtual clock.
* USB: absent or a stub; the black-box/USB paths are not needed to render the UI.
* Watchdog: never fires.

## Milestones

| # | Milestone | Acceptance |
|---|---|---|
| M0 | Core spike | app image runs; first unimplemented-SPR profile produced; core route chosen |
| M1 | RAM + SPI flash | firmware reads resources; asset bytes match the dump |
| M2 | Display | first framebuffer PNG is a legible screen |
| M3 | Timer/IRQ | UI pumps; main menu renders |
| M4 | Keys | UP/DOWN/OK navigate the menu |
| M5 | Validation | boot-to-menu test + framebuffer hash diff vs the device |

## Observability

* **SPR/MMIO trace** with names resolved from `ax329x.h`; log "unimplemented SPR"
  on first touch to drive the stub backlog.
* **Framebuffer PNGs** per frame; diff across builds and against device photos.
* **Coverage**: which firmware functions executed, to see what the emulator
  actually reached.
* **GDB stub** (if QEMU) for stepping; otherwise a scripted breakpoint on PC.

## Repo layout

```
tools/emu/
  README.md            how to run, current status
  run.py               load a dump, configure, run, dump frames
  platform.repl / ax3292.c   machine + SPR dispatch
  devices/spi_flash.py|.c
  devices/display.py|.c
  devices/timer.py|.c
  devices/gpio_adc.py|.c
  sfr_map.py           parse ax329x.h -> SPR number/name/group table
  tests/               boot-to-menu, asset-read, framebuffer regression
```

If QEMU is patched, keep the patch and its build script under `tools/emu/qemu/`
and pin the QEMU version.

## Risks / open questions

1. **QEMU or1k SPR hook** (M0) — the whole core decision rides on it.
2. **Which LCD block actually drives the panel** — SDK `LCDCTRL` (SFR5) vs the
   firmware's `0x8900` layer registers; and the OSD/scaler path.
3. **App entry and startup assumptions** — what the app expects the bootloader to
   have already done (clocks, SDRAM, data copy).
4. **Timing** — tick accuracy; use a virtual clock and decouple the UI rate from
   host speed.
5. **Undocumented register bits** — the SLEIGH drops some SPR writes; trust the
   SDK source and the firmware listings, not the decompiler, for hardware.

## Effort (rough, part-time)

M0 1–2 days · M1 ~1 week · M2 ~1–2 weeks · M3 ~1 week · M4 3–5 days · M5 ongoing.
The display is the largest single chunk.

## First spike (concrete)

Load the app region of `firmware/original.bin` at `0x02000000` in
`qemu-system-or1k -M or1k-sim -m 64`, set PC to the app entry and SP to
`0x027FFFFC`, run a bounded instruction count, and log the first
illegal/unimplemented-SPR access. That one experiment decides between the QEMU
route and a custom core, and produces the initial stub backlog.
