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

The functional devices (SPI flash, LCD, timer, GPIO, ADC) are **not** modelled
yet: SFR-group reads return zero and writes are dropped. The trace is what the
model is being built from. Busy-wait loops that poll a status bit therefore spin
until the trace limit or the timeout — the first one hit is UART0
(`0x9088` data, `0x908c` pending).
