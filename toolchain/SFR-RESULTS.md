# AX3292 `__sfr` implementation

## Result

Implemented `__sfr` in the patched GCC 4.9.1 C compiler installed at
`/tmp/ax3292-toolchain/prefix/bin/or1k-elf-gcc`.

- **308 SFR checks pass**, using generated instructions and a bounded reference model.
- The original **30 static compiler/link/runtime checks still pass**.
- All **239 SDK translation units compile** in a private SDK copy with four
  filename-case aliases, and the SDK linker command completes using all four
  vendor libraries plus the reconstructed libc/libgcc.
- No hardware execution, firmware packaging, flashing, or Windows-tool execution.
- Original SDK trees and `~/.src/ax3292-toolchain` remain unchanged.

The complete SDK ELF is a build artifact, not a verified flashable firmware:
`tests/sdk-sfr-probe/ax329x_sdk.elf`.

## Implementation

`__sfr` is a GCC named address-space qualifier, address space 1, with 32-bit
pointers. It works with the SDK's existing lvalue macros:

```c
#define SFR_AT *(volatile __sfr unsigned long*)
#define SDBAUD SFR_AT (0x9888)

unsigned long read_baud(void) { return SDBAUD; }
void write_baud(unsigned long value) { SDBAUD = value; }
```

The compiler emits `l.mfspr`/`l.mtspr` for SFR accesses and retains `l.lwz`/`l.sw`
for ordinary RAM accesses. Constant selectors fit the unsigned 16-bit instruction
immediate; computed selectors use a base register with immediate zero.

SPR addressing combines the register and immediate with **bitwise OR**, not
addition. Consequently, the compiler computes pointer offsets in a register
before issuing the SPR instruction. Tests include a base where `base + 4` differs
from `base | 4`, as well as indexed pointers and a pointer-incrementing loop.
The target marks SPR addresses mode-dependent so optimizers cannot narrow a
32-bit read to an unsupported byte/halfword access, even without `volatile`.
Use `volatile` for hardware registers when each read/write must occur.

SPR selector zero is valid (`SPR_VR`). GCC 4.9 has no per-address-space
zero-address-valid hook, so this target disables null-pointer-check deletion,
as the historical AVR target also does. Without that change GCC inserted a call
to `abort` after reading SPR zero. The tests check that no such call remains.

An SPR store conservatively clobbers the condition-code register: writing
`SPR_SR` can change the condition flag. Tests write status values that contradict
a preceding C comparison and check that the subsequent C result/branch remains
correct. The model uses the standard OpenRISC F bit for this check.

Changes are in `src/gcc/gcc/config/or1k/`:

- `or1k.h`: register the qualifier and SPR assembly operand formatting.
- `or1k.c`: address-space hooks, move expansion, condition-code effects,
  zero-selector handling, and diagnostics.
- `or1k-protos.h`, `predicates.md`, `constraints.md`, `or1k.md`: declarations,
  memory predicates/constraints, and dedicated SPR instruction patterns.

The existing `patches/gcc-ax3292-little-endian.patch` now includes **both LE and
SFR changes**, against the pinned original GCC source. Apply it once, not on top
of the old LE-only patch. `patches/manifest.json` identifies and hashes the
current complete patches; `export-patches.py` verifies their application.

## Supported scope and limits

Supported: aligned 32-bit integer reads/writes, compound assignments,
SFR-to-SFR copies, typed SFR pointers, pointer arithmetic, integer-address casts,
and volatile accesses. A typed SFR pointer remains four bytes.

The compiler rejects byte/halfword/wide/floating-point accesses, declared SFR
storage objects, implicit conversions to RAM pointers, direct cross-space pointer
casts, and the tested atomic builtin. To reinterpret an address intentionally,
cast through an integer. The spaces remain disjoint to prevent silently replacing
an SPR access with a RAM access.

Callers must supply valid hardware selectors and alignment. Optimized constant
addresses receive range/alignment diagnostics, but the compiler cannot guarantee
those diagnostics when an address has already become a register, including at
`-O0`. It does not insert runtime selector checks.

Only C is built/tested. The implementation has not established complete vendor
ABI compatibility, every possible vendor extension, interrupt/hardware semantics,
or the validity of the resulting firmware image on an AX3292 device.

## Validation

```sh
cd /tmp/ax3292-toolchain
bash build-toolchain.sh gcc
python3 tests/validate.py
python3 tests/validate-sfr.py
python3 prepare-sdk-probe.py
python3 probe-sdk.py --sdk tests/sdk-source --output tests/sdk-sfr-probe
python3 export-patches.py
```

`tests/validate-sfr.py` compiles with `-O0`, `-Os`, `-O2`, and `-O3` in both LE and
BE modes. Its small instruction model checks return values, exact SPR read/write
traces, pointer arithmetic, ordinary RAM writes, condition flags, and restored
stack pointers. Negative tests check clean compiler failures without `-Werror`
or internal compiler errors. It is a test model, not an AX3292 emulator.

Evidence:

- `tests/sfr-validation/report.json`, objects, ELFs, disassemblies and rejection logs.
- `logs/sfr-validation.log` and `logs/validation.log`.
- `tests/sdk-sfr-probe/report.json`, per-source logs, link map and ELF.
- `logs/sdk-sfr-copy-probe.log`.

## SDK integration

The unchanged original SDK now compiles 165/239 translation units; 74 stop at
case-sensitive includes, no longer at `__sfr`. The private copy resolves the
Windows filename assumptions with these aliases only:

| Alias | Existing target |
| --- | --- |
| `bwlib/maxlib` | `Maxlib` |
| `ax32_platform_demo/uitipsRes.c` | `uiTipsRes.c` |
| `ax32_platform_demo/uitips1Res.c` | `uiTips1Res.c` |
| `ax32_platform_demo/resource/res.h` | `RES.H` |

No SDK source text or vendor-library bytes were edited. Alias provenance is in
`tests/sdk-source-case-aliases.json`.

The resulting ELF is ELF32 little-endian OpenRISC with no-delay flag `0x1`.
The linker reports no errors. Its symbol table retains two undefined entries,
`CFG_FUNC` and `CLKTUN_VAL`, from unused `.equ` aliases in `spi_boot_cfg.S`;
that input object has no relocations. These are not evidence of a fully validated
boot configuration and should be reviewed alongside AX3292 packaging/startup.

Next steps: review startup/linker layout and packaging, extend vendor ABI tests,
and perform controlled runtime validation before considering flashing.

## Runtime fixes: newlib setjmp/longjmp for the AX3292 SPR map

The pinned newlib revision (`v2.3.0-1`, `gcc6-preview`) ships an OpenRISC
`setjmp.S` that assumes the generic SPR numbering. Three defects were found and
fixed before the routine was allowed to run on hardware.

1. Status register address. Generic OpenRISC uses SPR `17`. The AX3292 register
   file is grouped with `MAX_SPRS_PER_GRP_BITS == 11`, and `ax329x.h` defines
   `SPR_SR = SPRGROUP_SYS + (0x6 << 2) == 0x18` and `SPR_VR == 0x0`. The same map
   is confirmed by stock firmware: `ax32xx_sysIcacheInit` reads/writes
   `SPR_ICCR`/`SPR_ICAIR` at `0x2000`/`0x2008`, exactly the header's
   `SPRGROUP_IC + 0` and `SPRGROUP_IC + (0x2 << 2)`, and `ax32xx_int.h` performs
   interrupt critical sections through `SPR_SR`. `setjmp` read SPR `17` and
   `longjmp` wrote it; that slot is outside the documented map and writing it is
   unsafe. Both sides now use `0x18`.
2. `jmp_buf` size. `machine/setjmp.h` defined `_JBLEN 31` (124 bytes) while
   `setjmp` stores the status register at byte offset 124, so it wrote four bytes
   past the declared buffer. `_JBLEN` is now `32`; `sizeof (jmp_buf) == 128`.
3. `longjmp` result value. The pinned revision used `l.sfne r4, r0` before
   `l.bf 1f`, which inverts the standard behaviour: a non-zero argument returned
   1 and a zero argument returned 0. Later upstream revisions use `l.sfeq`;
   matching that. A later checkout
   (`~/.src/ax3292-toolchain/src/newlib`, 2022 revision) confirms the change.

Syntax note recorded while reviewing `l.mtspr` encodings: `l.mtspr rA, rB, K`
writes `rB` to `SPR (rA | K)`. Unlike `l.mfspr`, bits `15:11` hold `rB`, so the
16-bit SPR immediate is split across bits `25:21` (high) and `10:0` (low). The
assembler, `objdump` and the firmware's ICCR read-modify-write agree on this.

Offline validation after the fixes: 160 build profiles with 127 ready and 33 over
the code budget, 20 compiler rejection checks, 923 instruction-model vectors and
104 repository regression tests. `_JBLEN` and the `longjmp` result are exercised
by the suite's `setjmp` case; the SPR map and both `__sfr` addressing forms are
exercised by the new read-only `spr_map` case.

Hardware results (KC02, 2026-10-08). All four `setjmp` and all four `spr_map`
variants pass, so the ported runtime and the SPR addressing are verified on the
device. The aggregate covering all 127 runnable profiles/923 vectors is in the
`kc02-fw-mod` repository at `patched/toolchain-tests/hardware-validation.json`.

The `spr_map` probe confirms the SPR map on silicon. `SPR_VR` reads `0x65803200`
and `SPR_PICVR` reads `0` identically through the C `__sfr` front end, the
selector form `l.mfspr rD,r0,K` and the register-base form `l.mfspr rD,rA,0`, at
all four optimization levels. `SPR_PICVR` is group 9 (`0x480c`) and `SPR_ICCR` is
group 4 (`0x2000`), so the grouped encoding and the hardware's `SPR = (rA | K)`
address formation are both confirmed. `SPR_SR` reads `0x0000c086`/`0x0000c486`
with `FO`, `EPH`, `LEE`, `IEE` and `TEE` set and `DCE`/`ICE` clear; cache enables
on this core live in `SPR_ICCR`/`SPR_DCCR`, so restoring `SR` cannot disable
caching, which de-risks the `longjmp` restore. `SPR_ICCR` bit 0 is set, matching
the enable bit the stock cache routine manipulates, but bits 8-9 were observed to
change between adjacent reads; that is recorded as an unexplained property.
