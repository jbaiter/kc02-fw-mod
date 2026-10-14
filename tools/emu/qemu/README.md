# QEMU patch for the AX3292 machine

`build-qemu.sh` fetches QEMU, applies `0001-ax3292-machine.patch` and builds a
`qemu-system-or1k` with an `ax3292` machine.

## Why a patch is needed

The chip is an OpenRISC ORBIS32 core, so QEMU's `target/or1k` is the right base,
but upstream differs from it in four ways that all matter. Each difference was
established from the firmware image and the vendor SDK, not assumed:

| # | Upstream QEMU | AX3292 | Fix |
|---|---|---|---|
| 1 | branches have delay slots | **no delay slots** (`-mno-delay`; in `original.bin`, 55% of branches are immediately followed by another branch, e.g. 878 `l.jal`→`l.jal`) | `nodelay` flag; `delayed_branch` commits in 1 step, and `l.jal`/`l.jalr` link to `pc + 4` |
| 2 | hardcoded big-endian (`mo_endian()` returns `MO_BE`) | **little-endian** (reset SR has `LEE`) | `little_endian` flag; `mo_endian()` honours it for fetch and data |
| 3 | unknown SPRs are **silently ignored** — `mtspr` drops the write, `mfspr` returns the destination register's stale value, with no machine hook | every peripheral is an SPR | `spr_hook` on the CPU, consulted before the built-in switch |
| 4 | group-0 SPR numbers are the OpenRISC spec | different numbering: `SR 0x18`, `NPC 0x14`, `PPC 0x1C`, `EPCR 0x20`, `ESR 0x40`, `EVBA 0x50`, `VR 0x00` | the machine remaps them onto QEMU's CPU state |
| 5 | exceptions vector to `exception << 8` | an **8-byte stub table** indexed by exception, based at `EVBAR` | `vect_pc = env->evbar + (exception << 3)` |

Difference 5 is easy to miss: upstream's `or1k_reset 0x100`, `or1k_bus_error 0x200`, …
layout is a vector table this chip does not have. Its dispatch is twelve 8-byte stubs
at `0x174`..`0x1cc` (VMA), each loading an exception code into `r3` and jumping to a
common entry at `0x1d4`. The repo's SLEIGH language had the generic or1k vectors
*removed* for the same reason (see `docs/BOOT_MAP.md`). The machine sets
`evbar = 0x16c` at reset, which is what the boot ROM does before entering the
application.

The vendor compiler emits the `__sfr` byte address verbatim as the SPR number, so
`SPR = (group << 11) | (index << 2)`; `tools/emu/sfr_map.py` holds the table and
the evidence for each entry.

## What the patch adds

* `target/or1k`: `env->nodelay`, `env->little_endian`, and `env->spr_hook`.
* `hw/or1k/ax3292.c`: the `ax3292` machine — 64 MiB flat RAM, `-kernel` loaded at
  `0x02000000`, entry `0x0200046C`, stack `0x027FFFFC`, and an SPR bus that remaps
  the architectural registers and logs every peripheral access as

  ```
  spr <R|W> <spr:04x> <value:08x> pc=<pc:08x>
  ```

  Set `AX3292_SPR_TRACE=0` to silence it, `AX3292_SPR_TRACE_LIMIT=N` to bound it
  (default 300000), and `AX3292_ENTRY=0x...` to override the entry.

## Timers, interrupts and `-icount`

Two runtime details matter once the firmware starts polling for a tick:

* **Pass `-icount`.** A tight firmware loop (`while (1) delay(50);`) executes as one
  chain of translation blocks, and QEMU only processes `QEMU_CLOCK_VIRTUAL` timers
  when it leaves that chain — so a normal run delivered exactly *one* interrupt and
  then stopped. `-icount shift=6` ties the virtual clock to the instruction count and
  checks timers at every block boundary, which also makes runs deterministic.
* **BQL discipline.** Timer callbacks run with the big QEMU lock held; SPR helpers do
  not. So `cpu_interrupt()` from a helper needs `bql_lock()`/`bql_unlock()` (as
  QEMU's own PICMR code does) while the timer callback must not take it. Getting this
  backwards asserts in one direction or the other.

## Status

The firmware boots through its whole early init; the live UI needs the timer
interrupt, which is where work stopped. What runs today is described below.

Modelled, each added only when the trace blocked on it:

| Device | Registers | Notes |
|---|---|---|
| UART0 | `0x9080`–`0x908c` | TX is instantaneous so the `PND` poll succeeds; output goes to stdout (run.py saves it separately from the trace) |
| SPI0 flash | `0x9000`–`0x925c` | byte mode with a command interpreter (0x9F/0x05/0x03/0x0B), plus DMA. Backed by the dump. The vendor SDK's `SPIFSADR = 0x6000000\|0` means the DMA **continues** the address the byte stream shifted in — that is the subtlety that matters |
| LCD | `0xA800`–`0xA87f` | generic register file; `LCDSTA` bit 31 reads set (the driver polls it as the completion flag) |
| everything else | `>= 0x8000` | generic read-back-what-was-written store, which the GPIO and LCD read-modify-write code needs |
| PIC | `0x4800`–`0x4814` | mask/status/version, plus the timer's IRQ0. QEMU models a PIC too but at different offsets (its PICSR is `0x4802`, the chip's `0x4808`) |
| TMR0 | `0x9800`–`0x98a8` | period/counter/control and the `BIT(5)` pending flag the ISR clears; drives IRQ0 on a 10 ms virtual tick |

What the firmware does now, from a cold start at `0x0200046C`:

```
sdram *saddr=0x2103980,*eaddr=0x27fec00        <- UART
RC128k=0x0
flash ID read (0x9f) -> EF 40 16
table reads from flash (byte mode + DMA)
clock/LDO/RTC/DAC/ADC/GPIO init
LCD panel init: LCDCTRL, LCDSTA, LCDCMD sequence
...lands in the RTOS idle loop at 0x0200c64c */ delay(50)
```

It gets through all of that and stops in the idle loop, which only the **timer
interrupt** can break out of. The timer and PIC are modelled well enough that the
interrupt is delivered and serviced, but it is dispatched through the boot SRAM's
exception stubs -- whose full set-up the boot ROM performs via `exception_init`,
which starting at the application entry skips. Hence the next step: start from the
reset vector and let the boot code set the exception machinery up, rather than
entering the application directly.

Entering through the boot code's own `_start` (`AX3292_ENTRY=0x320`, which calls
`exception_init` and *then* the application) is the right shape but pulls in the
boot's SDRAM set-up first: the tuning loop writes an opcode to `SDRHCMD` (`0x98c4`)
and polls bit 6 for completion, and it is reached before anything else useful.
Modelling that done bit naively moved the loop around rather than past it, so the
next step is a small SDRAM-controller model alongside the boot entry. RAM is just
RAM here, so the tuning results do not have to be *right*, only terminating.

## Finding a stall

A stuck firmware loop often touches no registers, so the SPR trace shows nothing.
Set `AX3292_PC_SAMPLE=1` to have the machine print the PC and GPRs every 500 ms:

```
pc 0202af94 ppc 0202af90 r1=027fffc0 r2=20000000 r3=00072b99 ... r9=0200c654
```

`r9` is the return address, which is how the retry loop above was found in one
step (do not reach for `-d exec`: its per-block logging slows TCG by orders of
magnitude and puts the sampled PC in the wrong place).

## The app's UI machinery (mapped from RAM and the listing)

The firmware's UI is a small mode machine.  All of it runs in SDRAM, so it is
readable from the listing once the addresses are known:

| what | where |
|---|---|
| UI mode variable (current mode; 12 = none) | `0x020ccd50` |
| previous/requested mode | `0x020ccd4c`, `0x020ccd54` |
| current mode's descriptor | `0x020ccd58` |
| mode table, 12 entries | `0x020ccd5c + mode*4` |
| UI pump (called from `main` at `0x02000528`) | `0x02008188` |
| mode request | `0x02008108` |
| per-mode install list -> 46-entry handler table at `0x020ccbd4` | `0x02007938` |
| LCD layer setup (layer A `0x8900/04/08/0c`, layer B `0x8910/14/1c`) | `0x0202b504`, called from `0x0204fexx` / `0x0205bxxx` |

`0x02008070(id, descriptor)` is not a device registration (I had it wrong at
first): it fills **the mode table**, so the nine calls at the application entry
register nine UI modes.  Mode 1's descriptor (`0x020c3334`) has a handler
pointer at `+8`.

The pump's loop:

1. read the mode; if 11, return;
2. if the current descriptor is NULL, set `0x020ccd54 = 1` and
   `0x020ccd58 = table[1]` -- i.e. **it defaults to mode 1**;
3. call `*(descriptor + 8)`, the mode's setup;
4. loop.

Live state while the app idles: mode `0x020ccd50 = 12`, descriptor
`0x020ccd58 = 0`, table entries 1..10 populated with the mode descriptors the
application registered.  So the pump keeps defaulting to mode 1, whose setup is
`0x020283cc`, and the UI never reaches the mode `main` asked for (6, main menu)
-- `ui_mode_request` only records the argument in mode 6's descriptor, it does
not switch the mode.

Note the app never writes the LCD layer registers, i.e. no framebuffer is ever
installed, which is why nothing can be displayed yet.

## The boot's retry loop, and the RTOS time base (latest state)

With the artificial SD/key injections removed (they were wrong -- the camera
shows its UI with no input at all, and injecting "card present" every tick fought
the app's own state machine until it wrote a pointer value, 0x020ce9fd, into
PICMR), the app spends all its time in one place.  A PC sample over 30 s lands on
only 7-8 distinct addresses:

    0x0202bc8c / 0x0202bca4   wdtKick (watchdog)
    0x020073c4 / 0x020073c8 / 0x020073cc    the retry loop
    0x020524a0                reads the flag byte at 0x020d1c44
    0x0202f878                the ADC EOC poll

That loop is

    0x020072c4 .. 0x02007348
        jal 0x020785a8, jal 0x0202b0d4 (malloc, 0x0203e5c4 walks the free list)
        jal 0x0205b51c, jal 0x020524c0 (writes the flag)
        jal 0x02021894 (reads the RTOS time), 0x0202f248, 0x0202f33c, 0x02038498
        loop while (flag != 1 || time_delta <= 1000)

so it is a ten-second retry of something that never becomes ready, not a dead
wait on the flag (that byte does read 1, and with IRQ0 kept alive the counters
advance: 0x020cd6fc steps 0x97 -> 0x98 and the ms counter at 0x020cd700 by 10 per
tick).

The RTOS time base is the interesting part: the 10 ms counter at 0x020cd700 is
advanced by exactly one instruction, 0x0202185c, inside the handler at
0x02021824 -- which the app registers as timerISR[0] (0x020d6af0), i.e. the
*timer0* interrupt.  Yet the app's own final PICMR (0x8030) masks IRQ0.  Either
the chip's PIC bit for timer0 is not bit 0 (the SDK's IRQ_TIMER0 is 0, but that is
a software enum), or something re-enables it later.  AX3292_FORCE_IRQ0=1 ORs
bit 0 into every PICMR write, which makes the time base run; the app then cycles
through its retries but still does not draw within 180 s, so what it waits for is
a device, not time.

The app's heap is sane: pool 0x02103980..0x027fec00 with the free list at
0x020cec6c, and malloc returns blocks.

Remaining suspects, all unmodelled: the sensor/ISP (H63P over I2C, CSI), USB, and
the sub-systems serviced from the RTC ISR at 0x02051a00 (the flag's owner).  A
camera shows its UI with no card and no cable, so the sensor is the prime
candidate.

## Latest: the only image in RAM is noise

A full 7 MiB dump of the SDRAM pool (0x02100000..0x02800000) at the end of a
120 s run contains exactly one image-like window: 76,800 bytes with 256 distinct
values and a black upper third, at 0x02170800 -- which is the same data as the
fourth LCD buffer (0x0216e480) seen 0x2380 bytes further in.  So the firmware
produces one 320x240 buffer full of noise and never paints the UI into the
framebuffers it configured.

Everything the firmware touches is modelled except the ISP/sensor, so that noise
is most likely the capture path doing what it can with an unmodelled sensor.
The boot flow's work loop (0x020071c4, work at 0x020072c4) is not stuck: its only
exits are malloc returning NULL (it does not) or a value mismatch, and it loops on
purpose, waiting for the flag at 0x020d1c44 (which reads 1) with a 1000-unit
timeout so it re-runs its work about once a second.  The UI pump at 0x02008188 is
only reached from main at 0x02000528, which the firmware never gets to.

Next: log the return values of the loop body's four calls (malloc 0x0203e5c4, the
ADC, 0x0205b51c, 0x02038498) to find the one that fails, and model the ISP/CSI so
the capture is not noise.
