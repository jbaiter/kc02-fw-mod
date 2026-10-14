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

## Status

Modelled, each added only when the trace blocked on it:

| Device | Registers | Notes |
|---|---|---|
| UART0 | `0x9080`–`0x908c` | TX is instantaneous so the `PND` poll succeeds; output goes to stdout (run.py saves it separately from the trace) |
| SPI0 flash | `0x9000`–`0x925c` | byte mode with a command interpreter (0x9F/0x05/0x03/0x0B), plus DMA. Backed by the dump. The vendor SDK's `SPIFSADR = 0x6000000\|0` means the DMA **continues** the address the byte stream shifted in — that is the subtlety that matters |
| LCD | `0xA800`–`0xA87f` | generic register file; `LCDSTA` bit 31 reads set (the driver polls it as the completion flag) |
| everything else | `>= 0x8000` | generic read-back-what-was-written store, which the GPIO and LCD read-modify-write code needs |

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
interrupt** can break out of. That is the next device (and then the interrupt
controller, the framebuffer dump, and the keypad).

## Finding a stall

A stuck firmware loop often touches no registers, so the SPR trace shows nothing.
Set `AX3292_PC_SAMPLE=1` to have the machine print the PC and GPRs every 500 ms:

```
pc 0202af94 ppc 0202af90 r1=027fffc0 r2=20000000 r3=00072b99 ... r9=0200c654
```

`r9` is the return address, which is how the retry loop above was found in one
step (do not reach for `-d exec`: its per-block logging slows TCG by orders of
magnitude and puts the sampled PC in the wrong place).
