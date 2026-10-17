# Handover — AX3292 firmware emulator (Stage 3: display + SPI flash)

> **Status note.** This document covers the *stock emulation* workstream and its
> "CURRENT STATE" sections name an old commit. For the current state of the
> project and the hardware plan, read
> [`HANDOVER_HARDWARE.md`](HANDOVER_HARDWARE.md), which is authoritative. The
> emulator facts and traps below are still accurate and still useful.

Written to survive a context compaction. Everything below is either verified
against the dump, the vendor SDK, or the vendor's own linked build; where
something is inferred or unconfirmed it says so.

> **Read "CURRENT VERIFIED STATE: alternate profiles and selfie" at the end first.**
> Earlier status sections are historical and contain superseded diagnoses.
> Use `tools/emu/qemu/README.md` for current build/run commands.

## Goal

Run a dumped AX3292 (KC02-family camera) firmware image on the host far enough to
**render its UI** — SPI flash and the display are the stated priorities, and the
display is the point of the exercise. Stretch: react to button input.

Earlier phases of the project (dump tooling, decompilation, rebuilt UI modules,
patches) are done and are not part of this workstream. This is only the emulator.

## Where things stand (one screen)

| | |
|---|---|
| firmware under test | the **original** stock image, sha256 `218c6cdf…` (verified) |
| boot | clean, single (56 B UART: `sdram *saddr=…` + `RC128k=…`) |
| RTOS tick | 100 Hz, high-priority, dispatched and acked |
| display | **configured and enabled** — 320×240, four `0x12c00`-spaced buffers |
| LCD frame ISR | running (~88× per 30 s) |
| keypad ADC | modelled and being scanned |
| watchdog | kicked continuously (normal, see traps) |
| **UI** | **not painted.** The last thing to fix |

Source tree: `~/.src/kc02-fw-mod`, branch **`reversing`**, last commit `b122a36`.
Working tree clean.

## Environment (rebuild/run from scratch if needed)

* **QEMU**: patched build at `~/.src/qemu-11.1.2/build/qemu-system-or1k`.
  Source at `~/.src/qemu-11.1.2` (upstream 11.1.2 tarball — *not* Debian's `+ds`
  repack, which strips `pc-bios` and breaks the build).
  Patch: `tools/emu/qemu/0001-ax3292-machine.patch`.
  Build script: `tools/emu/qemu/build-qemu.sh`.
  A minimal configure that works:
  ```
  ../configure --target-list=or1k-softmmu --disable-docs --disable-guest-agent \
    --disable-tools --disable-gtk --disable-sdl --disable-vnc --disable-spice \
    --disable-opengl --disable-curl --disable-libssh --disable-brlapi \
    --disable-capstone --disable-slirp
  ninja -qemu-system-or1k      # ~1 min on 16 cores
  ```
  If configure fails on python venv/meson: put the `meson==1.11.1` and
  `pycotap==1.3.1` wheels in `python/wheels/` (the Debian repack removed them).
* **App slice**: `/tmp/app.bin` = `firmware/original.bin[0x2600:0xd3200]`
  (byte-identical, 855040 B). Regenerate with
  `dd if=firmware/original.bin of=/tmp/app.bin bs=1 skip=$((0x2600)) count=$((0x0D3200-0x2600))`.
* **Toolchain**: `/tmp/ax3292-toolchain/prefix/bin/or1k-elf-{as,objcopy,objdump}`
  (little-endian, no-delay variant).
* **Vendor references** (very useful, see below):
  * SDK: `~/.src/ax3292-sdk/firmware0.96v2_totest`
  * **vendor's linked build**: `…/ax32_platform_demo/output/ax329x_sdk.lst`,
    `.bin`, `.map` — the boot section's true VMAs and symbols.

### Run it

```sh
Q=~/.src/qemu-11.1.2/build/qemu-system-or1k
AX3292_FLASH=$PWD/firmware/original.bin \
$Q -M ax3292 -m 64M -accel tcg,thread=multi \
   -display none -monitor none -serial none -kernel /tmp/app.bin
```

`-accel tcg,thread=multi` is **required** (see traps). `tools/emu/run.py` exists
and slices the image, but the direct invocation above is what has been used
lately; run.py predates several env knobs.

### Machine options (all env vars)

| var | default | meaning |
|---|---|---|
| `AX3292_FLASH` | – | the full dump, for the SPI flash model |
| `AX3292_CALLFIX` | **on** | corrects one boot-section call by 4 bytes (see below) |
| `AX3292_TICK_IRQ` | 0 | which PIC bit the tick raises (0 = timer0) |
| `AX3292_TICK_LP` | off | deliver the tick as LP instead of HP (negative result) |
| `AX3292_LCDC_IRQ` | on | raise IRQ4 frame interrupts once the display is enabled |
| `AX3292_FORCE_IRQ0` | off | OR bit 0 into every PICMR write (diagnostic only) |
| `AX3292_KEY` / `AX3292_KEYFILE` | 1023 | keypad ADC value for channel 3 |
| `AX3292_SDCARD` | off | fake SD presence (harmful, leave off) |
| `AX3292_SPR_TRACE` / `_LIMIT` | on / 300000 | SPR access trace to stderr |
| `AX3292_PC_SAMPLE` / `_MS` | off | periodic PC + SR/PICMR/PICSR/GPR dump |
| `AX3292_WATCH` / `_WORDS` | off | dump guest RAM when the ISR runs |
| `AX3292_FB` / `_FB_EVERY` | off | dump the panel as a PGM |
| `AX3292_RAMDUMP` | off | snapshot 0x02100000+0x700000 (SDRAM pool) |
| `AX3292_PROBE` | off | 8 comma-separated addresses sampled every 10 ticks, printed on change |
| `AX3292_PULSE_IRQ` | off | raise one IRQ line for half of every `AX3292_PULSE_PERIOD` ticks |
| `AX3292_PULSE_PERIOD` | 10 | period in ticks for `AX3292_PULSE_IRQ` (10 = 100 ms) |

## Verified hardware facts (the model's foundation)

**All peripherals are SPRs, not memory.** The vendor compiler emits a `__sfr`
byte address verbatim as the SPR number, so `SPR = (group << 11) | (index << 2)`;
`tools/emu/sfr_map.py` holds the table (1065 numbers) with per-entry evidence.

* **Group 0 is not the or1k spec**: `SR 0x18`, `NPC 0x14`, `PPC 0x1c`,
  `EPCR 0x20`, `ESR 0x40`, `EVBA 0x50`, `VR 0x00`. Hardware-verified in the repo.
* **Exception vectors are 16-byte slots**, `EVBAR + bias + exc*16`, with slot 5 =
  LP interrupt (`EXCP_TICK`) and slot 8 = HP interrupt (`EXCP_INT`) — matching
  QEMU's `EXCP_*` numbering. Out of the box QEMU uses `exc << 8` (wrong) and
  the stock or1k `SR_TEE` LP gate at bit 4 rather than this chip's bit 1.
* **The RTOS tick is the RTC (IRQ 5, `RTC_WDT`)**, not timer0: the app's PICMR
  ends at `0x00c08071` (bits 0,4,5,6,15,22,23) and the RTC ISR is the IRQ5 table
  entry. `RTCCON` (0x88e0) bits 0/1 are event flags; `RTCPND` (0x88e4) is
  write-1-to-clear; clearing the source deasserts the PIC line.
* The 10 ms counter (`0x020cd700`) is advanced by exactly one instruction,
  `0x0202185c`, inside the timer0 handler `0x02021824`.
* **Display**: `LCDCTRL 0xa800`, `LCDSTA 0xa804`, layer registers
  `0x8980..0x89a8` (SFR1 group 17) plus the `0xa8xx` panel/timing block.
  `LCDSTA` event bits are cleared by writing the bit **eight positions higher**
  (bit 2 → `0x400`, bit 7 → `0x8000`); clearing bit 2 runs the app's registered
  frame-complete callback at `*(0x020d7ebc+8)`.
* Frame-completion flags: `0xa048` (USB20_CSR0) bit 4 and `0x8910` bit 1.
* SPI flash: SFR2 `0x9000..0x92xx`; chip select is `SPIFGCON` bit 14, and the
  byte helpers rewrite that register per byte, so the command state machine may
  only restart on a CS **edge**. The DMA **continues** the address the byte
  stream shifted in (`SPIFSADR = 0x6000000|0` is a constant) — the address field
  is 25 bits.
* ADC/keypad: `ADCCON 0x9170` (`EN 0x10`, `GO 0x80`, `EOC 0x40`, channel in the
  low bits), `ADCDAT 0x9178`. **Channel 3 = PB1 = the keypad.**

## Application map (what the model drives)

| what | where |
|---|---|
| app entry the boot calls | `0x0200046c` |
| `fast_isr` (HP interrupt dispatcher) | `0x0202abc4` (see the 4-byte quirk) |
| RTOS tick handler | `0x02021824` (ticks++, +10 ms, then the software timer wheel `0x02021a0c`) |
| RTC ISR | `0x0202df68` |
| LCD ISR (IRQ4) | `0x0203584c` |
| UI mode variable / descriptor / table | `0x020ccd50` / `0x020ccd58` / `0x020ccd5c` |
| UI pump (called from `main` at `0x02000528`) | `0x02008188` |
| mode register (`uiModeRegister`, fills the table) | `0x02008070` |
| display enable + layer buffers | `0x898c` bit 31; buffers `0x02104080`, `0x02116c80`, `0x0215a480`, `0x0216e480` |
| allocator (`malloc`) / heap block | `0x0203e5c4` / `0x020cec48..5c` |
| the boot/work loop the app spins in | function `0x020071c4`, body `0x020072c4`, poll `0x02007328` |
| the handshake flag the spin waits on | `0x020d1c44` (byte); `getFlag` `0x020524a0`, `setFlag` `0x020524c0` |
| the module owning that flag (18 inline accesses) | `0x02051e70..0x02052768` |
| the only external caller of that module | `0x02051f4c` ← `0x02091b4c` (the USB/SCSI path) |
| HP dispatch | `fast_isr` `0x0202aca4`: reads SPR **`0x480c`**, `& 0x1f`, jumps via the pointer table at `0x020807f4` |
| the app's LCD init (the real one) | `0x02032db8..0x02032e50` — **not** `0x0202b504` |

## The one guest-code correction, and why it is justified

The emulator adjusts **one instruction** of the boot section by default
(`AX3292_CALLFIX`). The reasoning, established against the vendor's own build:

* the vendor's `_start` is instruction-for-instruction identical to ours, and its
  `_except_hpint` calls `fast_isr`;
* the vendor's `fast_isr` starts with `l.sw -4(r1),r9` (the red-zone save) —
  and our application has that identical prologue **at `0x0202abc4`**;
* our boot's `l.jal` targets **`0x0202abc8`**, and our boot's *immediate* call to
  the application's `main` (`0x0200046c`) is correct;
* therefore the boot was linked against an application **4 bytes larger**, the
  gap is between `main` and `fast_isr`, and only that one reference is affected.
  That is the "4-byte section-size difference" `docs/BOOT_MAP.md` records.

**With it off, the HP handler never returns** (its `r9` comes from a slot nothing
writes) and the firmware loops through `_start`. With it on: 60 EPCR writes in
30 s, one clean boot.

## Traps that already cost time (do not re-learn these)

1. **The watchdog polling is normal.** `wdtKick` (`0x0202bc7c`) runs hundreds of
   thousands of times. It is the app's idle/loop heartbeat, not a hang.
2. **`-d exec` is useless here** — its per-block logging puts the sampled PC in
   the wrong place. Use `AX3292_PC_SAMPLE` instead.
3. **Virtual-clock timers never fire in a tight guest loop** (the vCPU never
   leaves its TB chain) — that is why a naive run delivered one interrupt.
4. **`-icount` makes timers work but runs one instruction per main-loop round
   trip**: measured ~12 µs/instruction (~40 kIPS). Unusable. Use
   `QEMU_CLOCK_REALTIME` + `-accel tcg,thread=multi`.
5. **Never call QEMU's memory API from the tick callback** (`dma_memory_write`):
   it runs in the main thread, wants the BQL, and races the vCPU into an
   intermittent segfault. Use the RAM host pointer
   (`memory_region_get_ram_ptr`) as the SD injection and watch dump now do.
6. **Do not patch the guest without saying so.** `CALLFIX` and `FORCE_IRQ0` both
   modify the firmware, and earlier milestones ("display configured") were
   produced with them on. Preferences: keep `FORCE_IRQ0` off; keep `CALLFIX`
   documented; always state which is in play.
7. **Reading a stale PICMR gave a false story** ("the app masks timer0"). Its own
   sequence ends at `0x00c08071` **with bit 0 set**. Check the timeline of writes,
   not a single value.
8. **The `lcd` trace window was too narrow** (`0x8900..0x897f`) and reported "no
   display writes" while the config was landing at `0x898c`. It is now `0x89a8`.
9. `AX3292_SDCARD` fights the app's own state machine (it ended up writing a
   pointer value into PICMR). Leave it off.
10. **The SPR trace truncates and lies by omission.** It fills its 2 M lines in
    roughly 7 s, so a region that shows "0 accesses" late in the run may simply
    be past the cut. Early conclusions along the lines of "the tick handler never
    runs" came from exactly this. Use `AX3292_PROBE` for anything temporal -- it
    costs nothing and cannot truncate -- and treat trace counts as a picture of
    the *first few seconds*, not the run.
11. **`ram_ptr` is indexed by guest address directly.** The RAM region is one
    64 MiB block at guest 0, so SDRAM at 0x02000000 is offset 0x02000000. Do not
    subtract a base.

## Where it is blocked, precisely

The app gets a long way — clean boot, display configured, keypad scanned,
interrupts serviced — and then spends the rest of its life in a **1 kHz work loop
waiting on a software handshake flag that is never cleared**.

The loop is at `0x020071c4..0x020073ec`, entered from `main`. Its steady shape is:

```
0x020072c4  work: 0x020785a8, 0x0202b0d4, 0x0205b51c, setFlag(1)
0x02007320  t0 = time()
0x02007328  wdtKick()
0x0200732c  if (getFlag() == 1) goto 0x020072c4     /* keep working */
            else if (time() - t0 <= 1000) goto 0x02007328
            else goto 0x020072c4
```

**The polarity matters here and is easy to get backwards.** `l.bnf` after
`l.sfnei r11,1` branches when the comparison is *false* — i.e. when `r11 == 1`.
`getFlag` (`0x020524a0`) is `return *(u8*)0x020d1c44`; `setFlag` (`0x020524c0`) is
`*(u8*)0x020d1c44 = v`. So while the flag is 1 the loop keeps doing work, and the
only way out is the flag being **cleared**.

`AX3292_PROBE=0x020ccd50,0x020ccd58,0x020d1c44` tells the whole story in two
lines:

```
t=1    (10 ms)  ui_mode -> 0x0c, ui_desc -> 0x00000000, flag -> 0x00
t=19  (190 ms)                                              flag -> 0x01
```

and nothing changes for the rest of the run: the flag stays 1, the UI mode stays
12 (none), the descriptor stays NULL. So the app is *running* — it sets the flag
at 190 ms and keeps executing the work body — and **the counterpart that should
clear the flag never runs**. That is why the UI mode machine is never entered:
`main` never gets past this loop to its UI pump at `0x02000528`.

Every inline access to that flag lives in one module,
`0x02051e70..0x02052768` (18 of them), and the module's only external caller is
`0x02051f4c` ← `0x02091b4c` — the USB/SCSI command path.

Interrupts are *not* the answer: the app's PICMR enables bits 0, 4, 5, 6, 15, 22
and 23, and `AX3292_PULSE_IRQ` sweeps of 6, 22 and 23 change nothing (pulsing 15
early derails the boot because the USB handler is not ready yet). The two the
model does drive — 4 (LCDC) and 5 (RTC) — are already firing.

### Next step

Find what should clear `0x020d1c44`. In order:

1. Disassemble `0x02091b4c` and work out what calls it. If it is the USB/SCSI
   dispatcher, the missing counterpart is the command path — and then the
   question becomes why the UI is supposed to appear without a host (it is, per
   the user, so this may be a red herring).
2. Inspect the two registered LP callbacks the LP dispatcher loads and calls:
   `*(0x020d6ae0)` (stub `0x0202ac6c`) and `*(0x020d6ae4)` (stub `0x0202ac80`).
   Whoever installs them and whatever clears the flag are probably the same
   subsystem.
3. Consider the sensor/ISP as the counterpart. It is the only peripheral with no
   model at all, the single image-like window in a full SDRAM dump is *noise*,
   and the app has configured a preview-shaped layer. `AX3292_PULSE_IRQ` makes
   sweeping the remaining candidates cheap.

Two things worth knowing before diving in: the app never writes `NPC` (0x14) and
never writes `SR` from a syscall path, so **it never context-switches** — there is
no second task to wake. And the UI mode machine itself is intact: `main` requests
mode 6 (main menu) and the table holds modes 1..10, so once this loop is exited
the UI has somewhere to go.

### The interrupt map (read off the app's own table)

`fast_isr` (`0x0202aca4`) reads SPR `0x480c`, masks `0x1f`, and jumps through the
32-bit pointer table at `0x020807f4`. Decoded:

| IRQ | handler | IRQ | handler |
|---|---|---|---|
| 0 | `0x0202c978` | 14 | `0x0202ad28` |
| 1 | `0x0202c9bc` | 15 | `0x0202ad30` |
| 2 | `0x0202ca00` | 16 | `0x0202ad3c` |
| 3 | `0x0202ca44` | 18 | `0x0202ad44` |
| 4 | `0x0202c738` | 21 | `0x0202ad4c` |
| 9-13 | `0x0202ad84` (returns) | 22 | `0x0202ad54` |
| | | 23 | `0x0202ad64` |
| | | 26 | `0x0202ad5c` |
| | | 27 | `0x0202ad6c` |
| | | 29 | `0x0202ad74` |

The stubs at `0x0202ace0..0x0202ad84` are the handlers; several tail-call into the
`0x0202c7xx`/`0x0202c9xx` driver code. Note this fixes an earlier mistake in this
document: the LCD handler is **`0x0202c738`**, not `0x0203584c` — the latter sits
in a different dispatcher's table.

## Things already ruled out (do not redo)

* **Unicorn** (no OpenRISC backend); **Renode** (unverified).
* QEMU's `or1k-sim` machine as-is (big-endian, delay slots, spec vectors, no SPR
  hook) — hence the patch.
* `AX3292_FORCE_IRQ0` (masks the real problem).
* `AX3292_TICK_LP=1` (the app reboots on an unexpected LP `TICK`).
* `AX3292_SDCARD=1` (corrupts app state).
* Keypad idle value (0/50/900/950/1023) — the display config happens in all cases.
* Low-memory load offset (0x1fc/0x200/0x0) × app offset (0x2600/0x25fc/0x2604) —
  nine combinations, none makes the handler return; the boot/jal mismatch is real
  and is handled by `CALLFIX`.

## The JPEG blocks are already modelled (do not write them again)

`tools/emu/qemu/ax3292/ax3292-jpeg.h` already implements both hardware blocks with
libjpeg behind them, and it is more complete than the 2026-10-16 still-capture work
assumed:

* **JPGA encode** honours the guest's own quantization and Huffman tables (read from
  the decoder table RAM the guest filled at `0x66400`), the source and target sizes
  in `0xb878` / `0xb80c`, quality from `PRE_JPCON1` bits 23..26, the inline buffer at
  `0xb820`/`0xb824`, the output window from `0xb810`, the AVI prefix when info mode
  is 1, and manual mode: `ax3292_jpeg_a_tick` holds the encoder back while
  `0xb8a4 < (0xb878 & 0xffff)`, so `ax32xx_mjpeg_manual_on`'s 800-line trigger is
  what releases it. It publishes `0xb81c = out + length` and raises `0xb840` bit
  0x40 (or 0x48 on failure).
* **JPGB decode** searches the guest's bitstream for the JPEG SOI, honours the
  manual-stride bit in `0xc0c0`, scales to the programmed target with a 4:2:0 box
  filter (documented approximation), and reports completion in `0xc040`.
* Both raise their interrupt only when the guest's own mask enables it, so a ROM
  image that leaves `PICMR` alone is not disturbed.

So the "emulator JPEG model" is not missing work. What is missing is a way to run
*Luma's* RAM image inside the emulated machine, which is a different feature:

1. a machine property (or a `-device loader`) that places Luma's bench binary at a
   fixed base in guest RAM and a 2048-byte harness packet at `0x020d8800`;
2. a way to enter it *after* the stock firmware has booted, because `hardware_init`
   calls stock routines that expect the stock init to have run (PLL, SDRAM, heap);
3. `r3 = 0x020d8800` on entry, since `luma_bench_once` reads its options from
   `buffer + 1792`.

There is no PC-write path in the machine today (`-M ax3292` exposes wiring and boot
handoff, and key injection via `key-file`, but nothing that transfers control to a
second image). That is a QEMU-side change with its own interrupt/lock questions, and
it should be designed as such rather than bolted onto a test.

Until then the honest statement is: the emulator can prove the *stock* photo path
and can exercise these blocks through stock code, but it cannot yet exercise
`ax_jpeg_capture.c`, `ax_jpeg_decode.c` or the FatFs adapters, whose current
evidence is native unit tests plus one pending bounded hardware trial.

## Repo artifacts added by this workstream

* `tools/emu/sfr_map.py` + `sfr_map.tsv` — SPR number ↔ name (1065), with
  confidence and evidence per entry.
* `tools/emu/spr_census.py` — static census of SPR accesses in a dump.
* `tools/emu/run.py` — slice a dump and run it.
* `tools/emu/qemu/0001-ax3292-machine.patch` — the whole QEMU delta (machine +
  CPU hooks + vector formula).
* `tools/emu/qemu/README.md` — the detailed findings narrative (long; this
  handover is the condensed version).
* `docs/EMULATOR_PLAN.md` — plan + M0/M0.5/M0.6/M0.7 results.

---

## Update: the state machine, and a methodology fix

**The methodology fix matters more than the finding.** `l.jal` is opcode **1**, not
3, in this encoding (`l.j`=0, `l.jal`=1, `l.bnf`=3, `l.bf`=4; `l.movhi`=6,
`l.ori`=0x2a, `l.addi`=0x27, `l.sw`=0x37, `l.lwz`=0x38, `l.sb`=0x36). Earlier
caller scans used 3, so they were finding `l.bnf` sites and silently concluding
"no callers". Any conclusion of the form "nothing calls X" from before this fix
should be re-derived. The `movhi`+`ori` scans were correct and stand.

### The flag is a state struct, not a boolean

`0x020d1c44` is the base of a structure the owning module uses as:

| offset | use |
|---|---|
| +0 | **state**, compared against **1** and **2** |
| +1 | 0xff / 0 (a "started" marker) |
| +2 | 0 |
| +3 | 100 |
| +4 | 0 |
| +16, +20 | zeroed words |
| +24 | read as a byte |
| +40, +44 | words |

The writes are visible in the module: `0x02051fa4` sets state 0, `0x02051f1c`
sets state 0 and +2 = 0, `0x02052084` sets **state 2**, and `0x020524c0` is the
generic `setFlag`.

### What polls it (opcode-1 scan, authoritative)

`getFlag` (`0x020524a0`) is called from **20 sites**, and they are overwhelmingly
UI code:

    0x02000350 (startup)   0x0200a6f0   0x0200ab20   0x0200af34   0x0200b004
    0x0200bc4c   0x0200bd10   0x0200c2a8   0x0200d51c   0x0200d774   0x02015d20
    plus the loop itself (0x0200732c, 0x020073c8, 0x0200742c, 0x020075b4, 0x02020630, 0x020206e0)

`setFlag` (`0x020524c0`) is called from `0x02006ec4`, `0x02007308`, `0x0201f15c`
and `0x02020608`.

### The loop is UI/display code, not a task

The loop at `0x020071c4` is a **called function** with four callers:

    0x020074dc   0x0200943c   0x0200a6ec   0x0200ce40

Three of those are in the UI region (`0x0200a000..0x0200d800`), and its body
callee `0x0205b51c` has 32 callers all in the `0x02005xxx` display region. So this
is the display/UI update loop, and it is gated on the state reaching **2**.

That reframes the blocker: the app is not stuck in an unrelated task, it is stuck
in the update loop that would paint the UI, waiting for a state transition that
some other context has to make.

### Next step

The state-2 setter (`0x02052084`, in the function at `0x02051fcc`) still shows no
*call* anywhere, so it must be entered indirectly. In order:

1. Find `l.jalr` sites whose target is loaded from a global — that is where a
   runtime-installed handler would live. Look first in the UI/display code
   (`0x02005xxx`, `0x0200a000..0x0200d800`) and in `0x02006ec4`'s neighbourhood,
   since that function both calls `setFlag` and is called from the display path
   (`0x02038474`) and the sensor path (`0x0202f494`).
2. Note `0x02006ec4`'s callers: `0x02006c74`, `0x02038474`, `0x0202f494`,
   `0x0203e694`. That mix (display, ADC, USB) is the shape of a *shared status
   publisher*, which is a good candidate for whoever should move the state along.
3. `-d exec` restricted with `-dfilter 0x02051e70+0x1000` would settle whether the
   module executes at all, at the cost of being slow. It is the cheap way to
   confirm or kill the "unreachable setter" reading.

---

## BREAKTHROUGH: the UI renders

The blocker was the **audio engine**, not the display. With three fixes the app now
boots through to the UI and **paints real graphics**:

1. **Model the DAC (IRQ 6).** `sfx_play_asset` (`0x020071c4`, called by UI code)
   plays the startup jingle and spins while `media_busy_get()` (`0x020524a0`,
   byte `0x020d1c44`) is set. That flag is driven by the audio state machine at
   `0x02051fcc`, whose `event & 4` branch is the "playback finished" transition.
   The event comes from the generated DAC ISR, `ax32xx_dacIRQHandler`
   (SDK `ax32xx_dac.c`), which tests `DACOBSTA` (`0x9150`) bits 1/2/4 in order and
   calls the registered handler with that bit. My model returned a stale store for
   `DACOBSTA` — always zero — so the handler never fired and the jingle never
   ended. The DAC now free-runs once configured (TMR0 `0x9140` write latches it),
   raising the buffer-empty bit; the guest acks through `DACOBSYNC` (`0x914c`),
   which keeps it level-triggered rather than a storm. **This is the fix that
   unblocked everything.**
2. **Model the engine at `0x9880`/`0x9884`.** `0x0206874c..0x02068784` starts it
   via `0x9884` and polls **bit 17 of `0x9880`**, timing out against the
   free-running counter at `0x9908`. Nothing in the SDK headers names it, so it is
   modelled by behaviour: a started operation completes two ticks later, and bit
   17 is cleared by writing it back.
3. **Bound the SFR backing store.** The firmware writes SPR `0x100010` (it
   computes SPR numbers from registers); my generic store indexed
   `sfr[spr >> 2]` unchecked and **segfaulted QEMU**. The chip silently ignores
   unimplemented SPRs — a behaviour already documented in this file — so the model
   now does the same. In gdb this was unambiguous:
   `ax3292_reg_write(spr=1048592) at ax3292.c:1148  s->sfr[spr >> 2] = v;`

Timeline now (`AX3292_PROBE=0x020d1c44,0x020ccd50,0x020ccd58,0x020d416e`):

    t=1     (10 ms)   state 0, ui_mode 0x0c (none), desc 0
    t=19    (190 ms)  state 0 -> 1
    t=22    (220 ms)  state 1 -> 0                      audio done
    t=182   (1820 ms) desc 0 -> 0x020c4c18              a mode descriptor is installed
    t=2105 (21050 ms) desc -> 0x020c3334                mode switched
    t=2107 (21070 ms) state 0 -> 1, then 1 -> 0         and round again

and the UART is no longer just the two-line banner:

    sdram *saddr=0x2103980,*eaddr=0x27fec00
    RC128k=0x708bfe
    memPool blkSize:12
    memPool freeBlks:500 / maxBlks:500 / minFreeBlks:500

### Framebuffers

The four layer buffers at `0x02104080`, `0x02116c80`, `0x0215a480`, `0x0216e480`
(320x240, one byte per pixel, `0x12c00` apart) are populated by the application
itself. `layer1` holds **a rendered camera icon with sparkles** — a real UI asset,
drawn by the app's own graphics code — which is the clearest proof yet that the
portrait is right end to end. `layer3` holds a partially drawn white panel with a
dark rule. `layer0` and `layer2` are still empty at the time of the dump.

    AX3292_RAMDUMP=/tmp/ram.bin ...   then slice 0x02100000+0x12c00 per layer

### Next steps

1. Run longer and dump repeatedly: the mode machine is still cycling, so the
   screen is mid-transition at 45 s. Establish what it settles into.
2. **Composite the layers** the way the panel does and expose that as the
   machine's display, so the emulator can be screenshotted directly instead of
   reading buffers out of a RAM dump.
3. Wire the keypad for real interaction (`AX3292_KEYFILE`), then the buttons can
   drive the menu.
4. The audio path is faked (a free-running DAC). It is enough to unblock the
   engine, but `audio_format_setup` (`0x02056f28`) does parse real WAV headers, so
   playback content is not modelled at all.

---

## Where the display work stands (after the audio fix)

`AX3292_FB=<fmt with one %u>` now writes a 640x480 PGM holding the **four layer
buffers side by side** (layer0 TL, layer1 TR, layer2 BL, layer3 BR), reading the
addresses from the panel registers (`0x8990`, `0x8994`, `0x8998`, `0x899c`)
rather than hardcoding them. Side-by-side rather than composited, because the
blend rule is not known and a guessed composite hides as much as it shows.

Observed, with `AX3292_FB_EVERY=100` (one frame per second):

* Early: **layer1 holds a cleanly rendered camera icon with sparkles** -- a real
  UI asset drawn by the application's own graphics code. This is the proof the
  pipeline works.
* By 9 s: it settles and stops changing. layer1 is then **empty** (the app
  cleared it), layer0 and layer2 are empty throughout, and **layer3 holds a
  vertical gradient** -- black for its top 129 rows, then a dithered fade to
  white. That is not text and not a drawn panel; it reads like an initialised or
  partially filled buffer.

### Open questions worth settling on hardware

* **The four panel addresses are probably not four layers.** Their spacings are
  `0x12c00`, `0x43800` and `0x14000` -- only the first equals one 320x240
  byte-per-pixel plane. The likely reading is Y/Cb/Cr planes of two images (a
  preview plus an OSD) rather than four stacked layers, which would make the
  camera icon in `0x02116c80` a luma plane of one of them. Worth confirming:
  which register is Y, which Cb/Cr, and what the panel actually scans out.
* **What is mode 1?** The descriptor slot goes `0 -> 0x020c4c18` at 1.8 s (mode 6,
  main menu) and then to `0x020c3334` at 21 s, which the earlier mapping calls
  mode 1. Mode 1 has no name in the symbol manifest. The settle into a gradient
  screen happens around that switch, so the two are probably related.
* **The gradient.** If it is a fade or a transition, it should be animated; it is
  static across a 70 s run. That suggests either a missing animation tick or a
  buffer the app fills and never revisits.

### Still unmodelled

JPEG (JPGA `0x0202c...`/IRQ 16, JPGB IRQ 22), CSI (IRQ 14), AUADC (IRQ 18),
rotate (IRQ 23), OSD compare (IRQ 27), MCP0-2 (IRQs 19/24/25), EMI (IRQ 26), and
the USB20 device controller (IRQ 15) beyond a stub. The IRQ names come from
`ax32xx/inc/ax32xx_int.h`, which is the authoritative list:

    TIMER0-3 0-3, LCDC 4, RTC_WDT 5, DAC 6, GPIO 7, UART0 8, SPI0 9, SDC0/1 10-11,
    SPI1 12, LVD 13, CSI 14, USB20 15, JPGA 16, IIC0_IIC1 17, AUADC 18, MCP0 19,
    BTUART 20, DE 21, JPGB 22, ROTATE 23, MCP1 24, MCP2 25, EMI 26, OSDCMP 27,
    PMU 28, USB11 29, USB11MCP 30, TT_MIPI 31

---

## Using the existing Ghidra project (do this first next time)

The firmware is already imported, analysed and **named** -- do not re-derive
anything with objdump.  The pi session logs show which project is canonical:

    /home/jbaiter/pi-ghidra-cache/artifacts/218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5/project/pi-ghidra.gpr

That artifact id **is our dump's sha256**, so our own image is analysed (a
sibling build, `d837019d...`, lives in `pi-ghidra-cache-kc02-or1k` and was
referenced far more often, but it is not our image).

Call the tool like this -- `binary` must be the **whole dump**, and `cacheDir`
must be the default cache, or it will try to re-import and fail with "no load
spec found" because a raw `.bin` needs the custom language:

    ghidra({ action: "batch", binary: "firmware/original.bin",
             cacheDir: "/home/jbaiter/pi-ghidra-cache",
             operations: [ { action: "decompile", address: "0x..." } ] })

The project's language is `KC02:LE:32:nodelay` with loader base `0x01ffda00`
(so file offset 0x2600 lands on CPU 0x02000000).  The decompiler output even
cites SDK prototypes, which is how the display path below was pinned down.
`tools/kc02_disasm_region.sh` covers regions with no defined function.

## The display path, from the decompiler

    0x02032d58  ax32xx_rotateStart(u8 src_mode, u32 src_width, u32 src_height,
                    u8 *dst_y, u8 *dst_uv, u32 dst_stride,
                    u8 *src_y, u8 *src_uv, u32 src_stride, u8 int_en)
                ROTATECON = src_mode*2 | 1 | int_en<<2;  ROTATESTA = 1;
                ROTIMGSIZE = ((h & ~1) - 1) << 16 | ((w & ~1) - 1);
                ROTSRCYADR = src_y & ~0x3f;   ROTSRCUVADR = src_uv & ~0x3f;
                ROTTGYADR  = dst_y & ~0x1f;   ROTTGUVADR  = dst_uv & ~0x1f;
                ROTADDRSTRI = (dst_stride >> 2) << 16 | (src_stride >> 2);
                ax32xx_intEnable(0x17, int_en);        /* 23 = IRQ_ROTATE */
                ROTATECON |= 0x80000000;               /* start */
    0x02032cec  ax32xx_rotateCheckBusy()  ->  ROTATECON >> 31
    0x02044378  hal_rotateKick(src, *src_img, *dst_img)
                polls CheckBusy ten times **with no delay between attempts** and
                spins forever in `do {} while (true)` if it is still busy.
    0x0203a11c  hal_lcdSetVCH(src, layer, x, y, w, h, vch_en) -- writes the
                in-RAM layer object at *(*(0x020ce960 + (src+1)*4) + 0xc),
                fields: +0 flag, +1 enable, +3 type, +4 x, +6 y, +8 h, +10 w.

Consequences for the model, all now implemented:

* `0x898c` is **ROTATECON**, not a display enable (an earlier note here was
  wrong).  Bit 31 must **clear immediately**: a busy window of even one tick is
  longer than `hal_rotateKick`'s ten back-to-back polls, so the UI hangs
  permanently.  This is why the app looked stuck.
* The engine must actually **copy** the source planes to the destination planes,
  using ROTIMGSIZE and ROTADDRSTRI, or the composed frame never reaches the
  panel buffer.  Implemented as a straight per-row copy of both planes; the
  rotation angle is not applied.

## Where the display work actually stands

The app renders the UI for real: `0x02116c80` has held a cleanly drawn camera
icon with sparkles, captured through `AX3292_FB` (a 640x480 PGM with the four
panel buffers side by side -- layer0 TL, layer1 TR, layer2 BL, layer3 BR).
With the audio, engine, SFR-bounds and rotate fixes, the boot is clean, the UART
prints `memPool blkSize:12 / freeBlks:500`, the mode machine advances and no
hang remains.

What is *not* yet right: the icon appears and is then cleared (a mode
transition), and the destination buffer keeps a stale gradient, so the final
composed screen has not been captured.  The next step is to follow the UI
renderers (`ui_render_state_0..5`, and the icon drawing around `0x020517e0`)
through the decompiler to see which buffer they target, rather than inferring it
from the rotate registers.

## The LCD driver, decompiled

    0x02035400  ax32xx_lcdInit()      sysClkSet(0x19,1); msDelay(0x14);
                                      LCDCTRL(0xa800) = 0; LCDCMD(0xa804) = 0x809f00;
    0x02035200  panel command write   toggles GPIO4 bit 0x10 (select) and bit 4,
                                      writes the command to 0xa858, then polls
                                      0xa804 *while bit 31 is set* (command busy)
    0x0203584c  ax32xx_lcdIRQHandler() -- the IRQ4 handler, reading LCDSTA 0xa804:

        0x80 -> clear with 0x8000, one callback
        0x04 -> clear with 0x400,  one callback
        0x02 -> clear with 0x200,  one callback
        0x08 -> clear with 0x800,  one callback, AND if (0xa800 & 0x30) == 0x10
                                   calls ax32xx_lcdKick()
        0x01 -> clear with 0x100,  one callback
        0x100010 together -> clear with 0x1000, one callback

    Every bit is cleared by writing it eight positions higher -- the rule this
    document already recorded, now confirmed from the driver.

**The model raises bit 2, but the true frame/vsync event is bit 8.**  Bit 8 was
tried and it *regresses*: UART falls from 142 B to 56 B and the layer buffers go
empty, because bit 8's path calls `ax32xx_lcdKick()`, which drives the panel, and
the panel is not emulated.  Bit 2's callback is a different one, but it keeps the
pipeline running far enough to render UI content, so bit 2 stays for now.  **The
panel is the missing peripheral.**  Modelling it (the GPIO4-bit-banged command
interface on 0xa858/0xa854, the command set, and the kick handshake) is what
should let bit 8 be used and the composed frame be scanned out.

`ax32xx_lcdIRQHandler` also shows the callback table: `JPEG_ram_020d7582` with
callbacks at `_2362_4_`, `_2366_4_`, `_2370_4_`, `_2374_4_`, `_2378_4_` -- i.e. a
structure at 0x020d7582 with a callback array around offset 0x2360/0x2370.

## Tooling note: Ghidra's coverage gap, and the helper for it

Ghidra has no function defined over much of the LCD/panel driver, so
`decompile` on e.g. `0x02035758` fails with "Function not found".  That is not a
setup problem: those routines are reached only through computed pointers, so
analysis never defined them.  The repo already provides the right tool:

    tools/kc02_disasm_region.sh 0x02035000 700 /tmp/lcd_region.tsv

which imports a read-only copy into a throwaway project with
`KC02:LE:32:nodelay`, loader base `0x01ffda00`, and linearly disassembles with
DumpShim.java.  Its output is `addr <tab> mnemonic <tab> text <tab> hex`.

## The panel, as far as it is decoded

The whole LCD block is:

    0xa800  LCDCTRL   the app writes 0x10 (seen), 0x11, 0
    0xa804  LCDCMD    commands such as 0x809f00 / 0x80889f00; bit 31 = busy,
                      polled by `do { v = 0xa804; } while (v < 0)`
    0xa808, 0xa80c    timing/config
    0xa854, 0xa858    the command data / index pair (0xa858 carries the command)
    0xa85c..0xa86c    more config, written once
    GPIO4 bit 0x10 = panel select, bit 4 = clock -- so it is a bit-banged
    interface, with a blocking wait on bit 31 after every command

The init sequence writes its commands from a loaded table, so the command
*values* need reading from that table (or from a memory dump at the right
moment) to recover the command set.

## Next step, concretely

1. Recover the panel command set: the values written to `0xa858` in
   `0x02035200..0x02035840`, which are loaded from a table just before each
   write.  Then find the command that carries the framebuffer base address --
   that is the surface the panel scans out, and therefore the one to capture.
2. Model the panel: select/clock via GPIO4, accept commands on `0xa858`, clear
   bit 31 of `0xa804` so the blocking polls return, and support the kick
   handshake.  Once that works, switch `AX3292_LCD_EV_FRAME` back to `0x08`
   (the true vsync event, whose callback runs `ax32xx_lcdKick`) and the composed
   frame should scan out.

---

# CURRENT STATE (authoritative — supersedes the sections above)

## Bottom line

The emulator boots the **original firmware**, configures the display, scans the
keypad, ticks, and drives the UI mode machine. It **renders the application's UI**
(real menu icons drawn by the firmware, captured to PNG) and **button presses
reach the UI** (proven end to end). The sensor is faked. Two things still stop it
being "done": the **boot is only ~40-50% reliable**, and the **compositor is never
seeded**, so the composed screen never appears.

Repo: branch `reversing`, last commit `2685b83`. The QEMU tree is restored to
exactly `tools/emu/qemu/0001-ax3292-machine.patch` and builds clean.

## How to run

```sh
Q=~/.src/qemu-11.1.2/build/qemu-system-or1k
AX3292_FLASH=$PWD/firmware/original.bin AX3292_SENSOR=1 \
AX3292_KEYFILE=/tmp/key.txt AX3292_FB='/tmp/fb/f%06u.pgm' AX3292_FB_EVERY=25 \
$Q -M ax3292 -m 64M -accel tcg,thread=multi \
   -display none -monitor none -serial none -kernel /tmp/app.bin
```

`/tmp/key.txt` holds one number, re-read on every keypad ADC conversion, so
buttons can be injected live: write `386` to press OK, `1023` to release.
`AX3292_SENSOR=1` fakes the image sensor. `AX3292_FB` writes a 640x480 PGM with
the four rotate buffers as a 2x2 grid (srcY, srcUV, tgtY, tgtUV) — read them with
image crops, **never** with byte slices: each quadrant is 320x240 inside a
640-wide image, so byte ranges are half-height bands and will mislead you.

Boot success is measurable: **142 B of UART** (prints `memPool blkSize:12 ...`)
means a clean boot; **56 B** means it stalled during bring-up.

## Requirements audit

| requirement | status | evidence |
|---|---|---|
| UI is rendering | **partial** | firmware draws a real camera menu icon into its 320x180 layer (captured); panel geometry in RAM is 320x180 (`0x140` x `0xb4`). Composition never happens (below), and only 1 of ~6 menu icons is drawn. |
| reacts to button presses | **proven** | inject 386 -> keypad scanner state `0x020c2598` becomes `0x1a` (OK) -> UI mode `0x020ccd50` changes state. Nothing happens without a press. |
| fake camera sensor | **done** | `AX3292_SENSOR=1`; preview plane measured live: 149/160 distinct states, up to 76,010 non-zero bytes. |

## Blocker A: the boot is ~40-50% reliable

Symptom: 56 B instead of 142 B. Not a crash — the firmware stalls in bring-up,
and it is **sensitive to host load** and **insensitive to my locking changes**.

The firmware **waits for LCD frame interrupts during bring-up**. Proven both
ways: delaying the frame event's gate made it fail 100% of the time, and it needs
them early. So the frame event is load-bearing for the boot.

Diagnosis attempt: a data race on `env->picsr`. The periodic tick runs on the main
thread holding QEMU's big lock; in MTTCG the vCPU thread executes guest code
*without* it; both read-modify-write `picsr`, so a lost update means the firmware
never sees its frame interrupt. Three fixes tried:

| attempt | result |
|---|---|
| take the big lock in the SPR hook | **worse** — starves the guest; the firmware hammers device registers ~43k/s |
| move the tick off `picsr` into an accumulated mask | **worse** — needs the mask cleared at every guest ack; the pass broke delivery |
| `qatomic_or` / `qatomic_and` on `picsr` | **no improvement** (2/6 clean) |

The atomics not helping is evidence that the race **is not the cause**. Better
hypothesis: it is **timing, not concurrency**. Frame events arrive at a fixed
*wall-clock* 100 Hz while the firmware measures its deadlines with its own RTC; if
emulation speed varies, the firmware blows its own deadline before enough frames
arrive. That fits load-sensitivity, insensitivity to locking, and the 100%
failure when the gate was delayed.

**Next step for this:** drive the frame event from *guest progress* (every N
instructions, or a vCPU-exit/tick-count hooked to execution) rather than wall
time, so the firmware's sense of time and frame delivery stay consistent. That
should make the boot reproducible, and everything after it verifiable.

## Blocker B: the compositor is never seeded (bootstrap deadlock)

`*(u32*)0x020ce974` (the display's current frame) stays **NULL**, and the
compositor's queue at `0x020d1ae0` / `0x020d1aec` stays **empty**.

```c
void hal_lcdSetFrame(s16 x,s16 y,u16 w,u16 h,void *p_lcd_buffer,u8 scan_mode)  /* 0x0203a0e0 */
{ hal_lcdDealBuffer(p_lcd_buffer, ...); }

void hal_lcdDealBuffer(void *frame)                                            /* 0x02039e44 */
{
  if (frame[0x2a] == 4) {                       /* type 4 = needs composing */
    p2 = hal_dispframeMalloc(1);
    if (p2 == NULL) hal_dispframeFree(frame);
    else            hal_rotateAdd(0, frame, p2);          /* <-- enqueue */
  } else {
    ax32xx_intEnable(4, 0);
    if (DAT_020ce974) hal_dispframeFree(DAT_020ce974);
    DAT_020ce974 = frame;                                 /* <-- becomes the display frame */
    if (*(sbyte *)(DAT_020ce980 + 0x27) == 1) ax32xx_deUpdate();
    ax32xx_intEnable(4, 1);
  }
}

void hal_rotateFrameDoneIsr(void)                                              /* 0x02044428 */
{
  hal_dispframeFree(_DAT_020d1af8);
  if (DAT_020d1af4 == 0) hal_lcdSetFrame(0,0,0,0,_DAT_020d1afc,0);
  else                   hal_osdDealBuffer(_DAT_020d1afc);
  ...
  for each of the two queue slots { pick one that is non-empty; clear it;
                                    hal_rotateKick(buf, ...); }   /* chain next */
}
```

Neither branch of `hal_lcdDealBuffer` is ever reached, so nothing submits the
first frame; `hal_rotateFrameDoneIsr` only runs from rotate-completion **IRQ 23**
(now raised, but gated on the firmware's own queue being non-empty), so nothing
completes and nothing is ever submitted.

**Next step for this:** Ghidra xrefs on `hal_rotateAdd`, `hal_lcdSetFrame` and
`0x020ce974` to find what should submit the *first* frame, and under what
condition. `hal_rotateKick` (0x02044378) currently executes **zero** times and
`0x8990` (rotate source Y) is written exactly **once** — a good cheap probe.

## Secondary: only one menu icon

A ~6-item menu draws one icon. Icons come from SPI flash through the SFAT asset
path (`sfx_play_asset` does an SFAT lookup near `0x0205b408`), so the likely cause
is the asset load succeeding once and failing after — or the menu legitimately
drawing only the selected item, which the D-pad test would settle (a DOWN press
should then change which icon is shown).

## Hardware facts confirmed this session

* **Keypad**: table at `0x020C268C`, 6 entries of 8 bytes, `{u8 key_id; u8 pad;
  u16 window}`: OK `0x1a`/386, RIGHT `0x1e`/776, LEFT `0x1d`/252, UP `0x1b`/657,
  DOWN `0x1c`/510, POWER `0x24`/12. `kc02_keypad_scan` (0x020035f8) reads ADC
  channel 3, matches that table, and needs a 3-sample debounce. State:
  `0x020c2598` last key, `0x020c87d0` debounce counter, `0x020c8784` last ADC.
* **ADC**: `ADCCON 0x9170` carries `EN 0x10 | channel` in the low bits, `GO 0x80`
  to start; `ADCBAUD 0x9174`; `ADCDAT 0x9178`. **Bit `0x40` of ADCCON is a BUSY
  bit, not a done bit** — `ax32xx_adcRead` (0x0202f85c) spins *while it is set*
  and only then reads ADCDAT. The model setting it is why no button ever worked.
* **ADC channel read path**: `hal_adcGetChannel` (0x0203814c) =
  `ax32xx_adcEnable(ch,1); ax32xx_adcSetBaudrate(1e6); ax32xx_adcConverStart();
  ax32xx_adcRead();`.
* **LCD event bits** (`LCDSTA 0xa804`), from `ax32xx_lcdIRQHandler` (0x0203584c):
  `0x80`->clear `0x8000`, `0x04`->`0x400`, `0x02`->`0x200`, `0x08`->`0x800`
  (this one also calls `ax32xx_lcdKick()` when `(0xa800 & 0x30) == 0x10`),
  `0x01`->`0x100`, `0x100010`->`0x1000`. Every bit is cleared by writing it
  eight positions higher. **Bit 8 is the true vsync**; the model uses bit 2
  because bit 8 derails the boot.
* **`ax32xx_lcdKick` (0x02035814)** is only `if (!DAT_020ce1f8) LCDCTRL |= 2` —
  a frame kick that real hardware self-clears.
* **LCD/panel**: `LCDCTRL 0xa800` (bit 1 = kick), `LCDCMD 0xa804` (bit 31 =
  command busy, polled `while (v < 0)`), `0xa858` = command byte, `0xa854` = data,
  plus `0xa808/0xa80c/0xa85c..0xa86c`. The interface is bit-banged through GPIO4
  bit `0x10` (select) and bit `4` (clock). Panel geometry **320x180**
  (`0x020ce984` = 0x140, `0x020ce988` = 0xb4).
* **Rotate engine**: `ax32xx_rotateStart` (0x02032d58) sets
  `ROTATECON 0x898c` (`src_mode*2 | 1 | int_en<<2`, then `|= 0x80000000` to start),
  `ROTATESTA 0x89a8 = 1`, `ROTIMGSIZE 0x89a0 = ((h&~1)-1)<<16 | ((w&~1)-1)`,
  `ROTSRCYADR 0x8990`, `ROTSRCUVADR 0x8994`, `ROTTGYADR 0x8998`,
  `ROTTGUVADR 0x899c`, `ROTADDRSTRI 0x89a4 = (dst_stride>>2)<<16 | (src_stride>>2)`,
  and enables **IRQ 23**. `ax32xx_rotateCheckBusy` (0x02032cec) is
  `ROTATECON >> 31`; `hal_rotateKick` (0x02044378) polls it **ten times with no
  delay** and spins forever if still busy — so the busy bit must read as clear
  immediately.
* **Layer objects**: table at `0x020ce960`, indexed `(layer+1)*4`; each entry
  points to an object whose `+0xc` is the real object: `+0` flag, `+1` enable,
  `+3` type, `+4` x, `+6` y, `+8` h, `+10` w. `hal_lcdSetVCH` (0x0203a11c) writes
  them.
* **Audio/DAC** (`IRQ 6`): `ax32xx_dacIRQHandler` tests `DACOBSTA 0x9150` bits
  1/2/4 and calls the registered ISR with that bit — the event the audio state
  machine at `0x02051fcc` dispatches on. `DACOBSYNC 0x914c` clears them.
  `sfx_play_asset` (0x020071c4) waits on `media_busy_get` (0x020524a0, byte
  `0x020d1c44`).
* **An unnamed engine** at `0x9880`/`0x9884`: started by writing `0x9884`, polled
  on **bit 17 of `0x9880`** with a timeout from `0x9908`.

## Tooling notes that save a lot of time

**Ghidra works on our exact image and is far better than objdump.** The canonical
project is the cache artefact whose id is this dump's sha256:

```
/home/jbaiter/pi-ghidra-cache/artifacts/218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5/project/pi-ghidra.gpr
```

Call it with the **whole dump** as `binary` and the **default** cache dir, or it
tries to re-import a raw `.bin` and fails for want of the custom
`KC02:LE:32:nodelay` language (loader base `0x01ffda00`):

```js
ghidra({ action: "batch", binary: "firmware/original.bin",
         cacheDir: "/home/jbaiter/pi-ghidra-cache",
         operations: [ { action: "decompile", address: "0x02032d58" },
                       { action: "references", address: "0x02044378", direction: "to" } ] })
```

It resolves a mid-function address to its function, accepts `name:` to decompile
by symbol, and the output cites SDK prototypes. **Coverage gap:** many LCD/panel
routines are reached only through computed pointers, so no function is defined
there and `decompile` fails with "Function not found". Use the repo's own helper
for those:

```sh
tools/kc02_disasm_region.sh 0x02035000 700 /tmp/lcd_region.tsv
```

**Naming SPR addresses from the SDK** (this cracked the rotate/panel question):
parse `#define NAME SFR_AT (SPRGROUP_SFRn + (0xNN<<2))` out of
`ax32xx/inc/ax329x.h` with `SFR0`=0x8000, `SFR1`=0x8800, `SFR2`=0x9000,
`SFR3`=0x9800, then look up the address you care about.

## Measurement hygiene (this cost real time)

* Check for other running emulators before measuring; competing processes can
  affect timing. Stop only test processes you own. Do not terminate a user's
  interactive session without permission, or use broad `pkill` commands.
* Report boot health as a **rate over >=6 runs**, never a single run.
* The SPR trace truncates at its line limit, and with the 1 kHz work loop it fills
  2 M lines in ~7 s — so any "this never runs" conclusion from late-run counts is
  unsafe. Use `AX3292_PROBE` for temporal questions.
* `AX3292_PROBE` samples 8 guest **RAM** addresses every 10 ticks and prints on
  change. It cannot read SFRs (they are not in RAM) — probing `0x9140` reads
  whatever RAM lives at that address and is meaningless.

## Traps already burned (do not repeat)

1. `0x898c` is **ROTATECON**, not a display enable. Its busy bit must clear
   immediately.
2. Raising the LCD frame event's bit 8, the rotate IRQ 23, or the frame event too
   early all derail the boot. Gate them on the firmware's own enable bits.
3. `AX3292_SDCARD` corrupts app state; leave it off.
4. `AX3292_TICK_LP` (deliver the tick as LP) makes the app reboot. HP is correct.
5. Do not take QEMU's big lock per device access — it starves this guest.
6. `-d exec` and `-icount` are both unusable here (accuracy and speed).

# CURRENT VERIFIED STATE: capture and printer

The original firmware runs unchanged: SHA-256
`218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5`.
The UI, native keypad, fake H63P/CSI preview, SDHC/FAT16 and photo viewer work.
Commit `b08198b` added JPGA encoding with guest JPEG tables and restart rows,
CSI snapshot preservation and a three-cold-boot capture/persistence regression.
Saved files are JRX wrappers; validate their separated packets without changing
SD bytes. The nominal 800 mV SRAM calibration is explicit ROM handoff data.
There are no guest instruction or UI/card-state patches.

Printer implementation:

- `hw/or1k/ax3292-printer.h` records GPIO DATA/CLK, LATCH, STROBE and motor phases.
  DATA/CLK are `0x800c` bits 15/13, LATCH/supply `0x8004` bits 5/6,
  STROBE `0x8000` bit 9. Paper/cover is active-low A7 while GPIO direction
  `0x8040` bit 7 selects input. Missing paper prevents printing.
- The stock driver clocks 383 dots into a 384-bit head. Preserve the retained
  bit. PNG tone uses normalized strobe counts; the firmware supplies grayscale
  and dithering. Four motor steps map to a row, with blank feed included.
  Raster is bounded to 384×8192. Supply-off publishes complete, numbered PNGs
  through exclusive `.part` reservations and hard links, preserving old output.
- JPGB samples DMA data at the kick and publishes completion after a minimum
  1 ms, through owning-vCPU tick work. Immediate completion precedes SDK
  post-start IRQ/callback setup; reading mutable registers at a later tick can
  decode the next UI job instead. IRQ22 respects the global/per-source mask;
  new DMA clears stale completion. JRX isolation adds EOI only in a host copy.
- Required host libraries: libjpeg/libpng; SDL2 for the interactive window.
  The distributable patch includes 13 QEMU sources. Do not rely only on edits
  in `~/.src/qemu-11.1.2`; regenerate and compare the repo patch after changes.

Launch (create the card once, close any emulator using it before reuse):

```sh
python3 tools/emu/create_sd.py /tmp/kc02-print-sd.img
python3 tools/emu/run.py firmware/original.bin --interactive \
  --sd-card /tmp/kc02-print-sd.img --printer-output /tmp/kc02-prints --gdb 1234
```

Kamera + Enter captures/saves/prints with stock defaults. Einstellungen →
Attribute drucken selects Punktmatrix/Graustufen; Drucken toggles printing.
`--printer-no-paper` requires `--printer-output`. GDB stays on localhost;
use `tools/emu/gdb.sh`, hardware breakpoints and byte memory reads. Do not share
locked SD backing files, inspect FAT while a guest writes, or stop user sessions.

Verification on the final implementation:

- `/tmp/kc02-printer-final`: dither and grayscale PNGs, no-paper with no PNG;
  all three cold boots also capture/view/return and preserve previous photos.
- `/tmp/kc02-post-printer-capture`: two captures, then a cold viewer-only boot;
  prior photo bytes unchanged and both JRX JPEG packets decode strictly.
- `/tmp/kc02-post-printer-viewer`: two boots through four fixture photos.
- `/tmp/kc02-post-printer-no-card`: automatic UI, real keys, camera motion/back.
- Full pytest: 132 passed, 5 skipped, 51 subtests; five pre-existing collection
  warnings. GPIO unit tests compile the header extracted from the repo patch.

Physical thermal spreading, temperature/energy calibration and exact platen
mechanics remain approximations. No new hardware validation occurred. Video,
short CSI line rings, USB devices, full ISP/audio and other camera builds remain
unsupported or unverified. SPI flash backing remains read-only; settings changes
need not persist across cold boots. Historical bootstrap/CALLFIX/queue/interrupt
race advice above is not a basis for new fixes.

## CURRENT VERIFIED STATE: alternate profiles and selfie

SDL2 headers are installed; Meson cache was cleared and the SDL-enabled binary
rebuilt. Keep evidence under `~/.cache/kc02-emulator/`; the reboot removed the
historical `/tmp` cards and traces listed above. Ghidra projects must use a
non-hidden path, such as `~/pi-ghidra-cache-alt-z12-disastmp`.

- `tools/emu/profiles.py` detects full dump hashes and supplies ROM handoff plus
  board wiring. Explicit `run.py --profile` handles modified dumps. All three
  builds load unchanged application bytes at `02000000`.
- X8D: app `2200..7f000`, entry `02000470`, SP `023ffffc`, VBG `1fec`, panel 270°.
  Z-12: app `2600..9a600`, entry `020005ac`, SP `021ffffc`, VBG `2230`, panel 90°.
- SR bit 14 is SRAM-vector selection, not upstream EPH. Low EVBA fixes X8D's
  relative ISR jumps. DE scan modes now account for reversed framebuffer origins.
- Both alternate sensors identify as BF3A03: I2C DC/DD, FC=3A. Z-12 has the selfie
  sensor. Its `sensorInit` at `02047dd8` drives PE10/PE11; the model chooses its
  bank from PE11, independently of the application camera byte at `0209e2a8`.
  The second bank produces horizontal bars/side luma and a white patch; the first
  produces vertical bars/bottom luma. BF3A03 register 1E controls orientation.
- **Z-12 keys use ADC channel 3, not 6.** The earlier channel-6 test was misleading:
  channel 3 defaulted to ADC512 and generated unintended native Enter events.
  Correct arrows are Up620, Down775, Left328, Right238; Enter512, Escape415.
  Tab aliases Right238 for native camera switching. P133 is the native print key;
  M704 is another function key. F is unwired. X8D uses 411/511 for previous/next,
  Enter788, Escape14, Space252 shutter, P788 and F132.
- Z-12 flash identity: JEDEC C84016, UID 4D37393830109B3F, copied from the dump's
  OpenPrinterImage header. SPI4B consumes four dummy bytes. UUID checks succeed;
  `OpenImage Credit Check Failed!` remains. Do not bypass it or seed app flags.
- CSI sensor-size events continue with output DMAs gated. SDK `hal_mjpegStart`
  depends on this. Automatic JPGA reads the CSI I2J buffers; manual mode uses
  B820/B824. DMA stride is a row-start increment; overlapping rows still complete,
  with bounds checked against the last actual Y/UV write.

Evidence and remaining work:

- `~/.cache/kc02-emulator/x8d-print-02`: cold UI, camera switch-free capture,
  preserved fixture bytes, PHO00005.JPG (4768×2688 and 672×384) and a 384×753
  thermal page (3008 steps, 0 bad steps, 33 tones).
- `~/.cache/kc02-emulator/z12-print-04`: cold UI, selfie switch and switch-back,
  capture, 4096×3072 and 640×480 packets, and a 384×675 thermal page (2696 steps,
  0 bad steps, 33 tones) with the firmware's date-stamp OSD.
- `~/.cache/kc02-emulator/z12-switch-back-final-{1,2}`: two cold UI runs with
  native PE11 change, a distinct frame, switch-back pixel comparisons and
  preserved fixture bytes. Screenshots are checked in as
  `docs/images/emulator/z12-{front,selfie}.png`.
- **Z-12 post-capture navigation was firmware-side, not a hardware wait.** After
  the shutter the firmware decodes/scales the 12 MP frame in guest code and
  drops key events until it finishes; a single Escape arrived too early. The
  regression now polls the photo-state bytes and retries Escape. Do not
  "fix" this in the model.
- **Both alternate builds print on the same GPIO head.** `800c` data/clock,
  `8004` latch/supply, `8000` strobe, identical to the original. The platen-motor
  phase order differs per board, so `Profile.printer_phases` and the
  `printer-phases` machine property were added. Z-12's observed forward order is
  `6,14,10,11,9,13,5,7`; the default is `10,8,9,1,5,4,6,2`. Before this, Z-12
  accumulated 16384 strobes on row 0 (`steps=0 bad_steps=1348`) and produced a
  384×1 page. `AX3292_DEBUG=1 AX3292_TRACE_PRINTER=1` logs vector transitions.
- Z-12 also links a Bluetooth JPEG printer (`open_printer_bluetooth_print_masf`,
  `printer_jpeg`, `printer_jpg_to_printer_wh`). That transport is not modelled;
  only the GPIO head is claimed.

Original regressions after the profile/CPU/CSI/JPEG/printer changes:
  `original-profile-final-ui` (two no-card boots),
  `original-profile-final-viewer` (two four-fixture viewer boots),
  `original-profile-final-capture` (two saves and a third cold viewer boot),
  `original-profile-final-printer` (dither/grayscale/no-paper, viewer and back).
  These runs include the final overlapping-row bounds change. Re-run after
  further shared backend edits. Full pytest: 139 passed, 5 skipped, 51 subtests,
  with five pre-existing collection warnings. Compiled backend tests use the
  distributed headers and cover sensor banks/IDs/orientation/DMA bounds, plus
  the printer's two phase orders (out-of-order vectors must not advance steps).
  SDL dummy-video smoke also produced a 320×240 scanout; this does not prove an
  OS window was visible.

`profile_smoke.py` automates native ADC keys and read-only observations. It
creates its own card and refuses existing output directories. No guest-memory
writes or application patches are involved.

The distributed patch applies to pristine QEMU 11.1.2 and byte-compares against
the 7 integration files it carries (`~/.cache/kc02-emulator/patch-check-integration`).
Patch SHA256: `24f7e08641ac9bdf0ee292e8bdf5df4fe2ebce47c57a2f74c5c5b3601dcbeff0`.
A persistent upstream archive is `~/.cache/kc02-emulator/qemu-11.1.2.tar.xz`;
`build-qemu.sh` now defaults to this cache rather than `/tmp`. No physical camera
testing has occurred. Use `tools/emu/README.md` for current alternate controls
and limits.

### 2026 hygiene pass

The device models are now ordinary repository source rather than text inside a
diff. `tools/emu/qemu/ax3292/` owns `ax3292.c` and the five device headers;
`build-qemu.sh` copies them into `hw/or1k/` and the patch carries only
`target/or1k/*`, `Kconfig` and `meson.build` (2,910 patch lines became 291).
Verified by extracting a pristine tarball into
`~/.cache/kc02-emulator/qemu-pipeline-check`, running `build-qemu.sh` against it,
and booting the resulting binary through `smoke.py` (`pipeline-ui`). Editing a
device model no longer needs a fresh tree.

Configuration moved from the environment to machine properties:

```sh
qemu-system-or1k -M ax3292,flash=original.bin,sensor=on,sd-image=card.img
```

`Profile.options()` in `profiles.py` builds them and `machine_args()` escapes
commas. Lists use `:` because a comma ends the `-M` option. `MachineClass` is not
a `DeviceClass`, so the properties use offset-based accessors, not `DEFINE_PROP`.

Diagnostics are gated: every `AX3292_TRACE_*`, `AX3292_DUMP_*` and
`AX3292_DEBUG_*` knob is inert unless `AX3292_DEBUG=1`, and setting one without
the gate warns and ignores it.

`AX3292_LOW` was deleted. It was unused by every tool, and its `base200` branch
set `vec_bias` on a CPU that did not exist yet, so it never did anything.

Every known approximation now carries a marker:

```sh
rg 'TODO\(approximation\)' tools/emu/qemu/ax3292   # currently 8
```

Remaining debt, in order: the device headers are still included into one
translation unit (correct for shared state, but it blocks per-device QOM
testing); the CPU core carries an intentional `nodelay`/little-endian/SPR-remap
fork; and the `hits`/`strobes` raster in the printer backend is allocated on
machine lifetime.

Regressions after the pass: original no-card UI and viewer, original capture
(three cold boots) and printer (dither/grayscale/no-paper), X8D capture+printer,
Z-12 selfie+capture+printer, plus the pristine-pipeline boot. Full pytest: 139
passed, 5 skipped, 51 subtests.
