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
