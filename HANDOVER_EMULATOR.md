# Handover — AX3292 firmware emulator (Stage 3: display + SPI flash)

Written to survive a context compaction. Everything below is either verified
against the dump, the vendor SDK, or the vendor's own linked build; where
something is inferred or unconfirmed it says so.

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
