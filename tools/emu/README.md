# AX3292 emulator tooling

Groundwork for running dumped firmware on the host. See
[`docs/EMULATOR_PLAN.md`](../../docs/EMULATOR_PLAN.md) for the plan and the
Stage 3 targets (SPI flash and display first).

## SPR addressing (the thing to get right)

Every peripheral on this chip is reached through OpenRISC special-purpose
registers, not memory. `l.mfspr`/`l.mtspr` take a 16-bit number that is the OR of
a base register and an immediate, so a constant access looks like
`l.mfspr rD, r0, K`.

The vendor compiler emits, for a `__sfr` object at byte address `A`, the number
`A` **verbatim** (verified against the reconstructed toolchain: `0x9244` becomes
`l.mtspr r0, r3, 0x9244`). The vendor header therefore works as the number table:

```
SPR = (group << 11) | (index << 2)
```

Two traps this tooling encodes:

* **Group 0 is not the OpenRISC spec map.** A probe on real hardware confirmed
  `SPR_SR = 0x0018`, and `0x11` (the spec SR number) is undefined on this core.
  The vendor's own `ax32xx/reset.S` uses `l.mfspr r9,r0,32` for EPCR and
  `l.mfspr r9,r0,64` for ESR, while `ax329x.h` labels `0x40` EPCR and `0xC0` ESR.
  `sfr_map.py` overrides the header here and records confidence per entry.
* **Aliases are common.** `ax329x.h` defines 1089 names over 1065 numbers.

## Tools

| Tool | Purpose |
|---|---|
| `sfr_map.py` | Parse `ax329x.h` into `SPR -> name/group`; look up a number; emit the table |
| `sfr_map.tsv` | Checked-in table (`name`, `spr`, `group`, `confidence`), regenerate with `--sdk` |
| `spr_census.py` | Static census of `l.mfspr`/`l.mtspr` sites in a dump |
| `run.py` | Slice the application out of a dump and run it on the `ax3292` QEMU machine |
| `qemu/` | The QEMU patch, its build script and notes |

```sh
# regenerate the checked-in map from the vendor SDK
tools/emu/sfr_map.py --sdk /path/to/ax329x.h --emit tsv > tools/emu/sfr_map.tsv

tools/emu/sfr_map.py --lookup 0x9244
tools/emu/spr_census.py firmware/original.bin

# build QEMU once (~2 min on 16 cores), then run a dump
tools/emu/qemu/build-qemu.sh
tools/emu/run.py firmware/original.bin --timeout 15 --trace-limit 40000
```

## Census caveat

The census only resolves accesses whose base register it could track as a
constant through a short linear scan. On stock `original.bin` it resolves about
57% of the 4760 access sites: 58 SPRs with ten or more hits carry most of the
traffic (status, clock, RTC, USB, LCD, PIC, flash), and a long tail of
single-hit numbers is noise from constants that were not SPR addresses. Treat
the ranked head as the device backlog and confirm it with a runtime trace once
the emulator runs.

## Status

The firmware runs and gets through its whole early boot. `qemu-system-or1k` is
patched with an `ax3292` machine (no delay slots, little-endian, an SPR bus),
and the stock image boots: UART log, flash ID and table reads, clock/LDO/RTC/
DAC/ADC/GPIO init, LCD panel init — then it settles into the RTOS idle loop,
which needs the timer interrupt. See [`qemu/README.md`](qemu/README.md) for the
device list and the M0.6 section of
[`docs/EMULATOR_PLAN.md`](../../docs/EMULATOR_PLAN.md).

Next: the timer + interrupt controller are modelled and the interrupt is
delivered, but servicing it reaches the boot SRAM's exception machinery, which
the boot ROM installs via `exception_init`. So the next step is to start from
the **reset vector** and let the boot code set that up before it calls the
application, rather than entering the application directly. After that: the
framebuffer dump (display) and the keypad.
