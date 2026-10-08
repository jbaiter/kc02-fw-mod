# AX3292 Linux toolchain reconstruction: first milestone

> Historical first-milestone report. The subsequent `__sfr` implementation and
> successful 239-file SDK compile/link are documented in [SFR-RESULTS.md](SFR-RESULTS.md).

## Result

Built and installed a patched OpenRISC GCC 4.9.1 C compiler, binutils 2.25,
libgcc, and newlib under `/tmp/ax3292-toolchain/prefix`.

`python3 /tmp/ax3292-toolchain/tests/validate.py` passes **30 static checks**.
The compiler produces native little-endian, no-delay ELF objects and links a
C fixture with the AX3292 vendor `libsd.a` object `ax32xx_sd.o`. The test uses
inert stubs for the object's unrelated hardware dependencies. It is not firmware.

No hardware execution, flashing, or Windows vendor-tool execution occurred.
The original AX3292 and AX3272 SDK trees remain unmodified.

## Sources and local changes

| Component | Upstream | Revision |
| --- | --- | --- |
| GCC | https://github.com/openrisc/or1k-gcc | `50e11f73098c722676829ae73caf09246e81cd44` |
| binutils | https://github.com/openrisc/binutils-gdb | `68b975af7ef47a9d28f21f4c93431f35777a5109` |
| newlib | https://github.com/openrisc/newlib | `476cdfd8d4189ca00679273b2602c5951d0112c3` (`v2.3.0-1`) |

These are reconstruction baselines, not recovered vendor sources. The historical
newlib revision `ce2a036647a4be46f3b27e1a531cc485df6f7548` could not be retrieved;
the build uses the available OpenRISC 2.3.0 release instead. Its configuration and
reentrancy ABI have not been verified against the vendor runtime.

Source archives and host dependency packages are in `downloads/`, with hashes in
`downloads/SHA256SUMS`. Original edited files are in `originals/`. Exported patches
are in `patches/`, with a file list and hashes in `patches/manifest.json`.
`export-patches.py` verifies that applying each patch reproduces the edited files.
The minimal endianness adaptation follows the approach in Samuel Holland's 2022
OpenRISC LE patch series, rather than applying that newer series unchanged.

Changes:

- Binutils: LE ELF target and default, assembler `-EL`/`-EB`, LE instruction/data
  encoding and disassembly, linker endian output selection, and rejection of
  mixed-endian input objects. Existing no-delay flag checks remain active.
- GCC: `-mlittle-endian`/`-mbig-endian`, LE default, conditional C byte/word/float
  layout, and forwarding endian options to assembler/linker.
- libgcc/newlib: conditional floating-point endianness definitions.
- newlib: `.nodelay` marking for its no-delay `setjmp.S` object. Its assembly
  already handles the two branch models.

## Build and use

The current build script expects the prepared sources and local host dependencies.
It is not an automatic downloader or a portable installer. On this host it uses
GCC/G++ 15.3.0, locally extracted bison/flex and MPFR/MPC headers, system GMP and
MPFR/MPC shared libraries. Older GNU source needs `-std=gnu89 -fcommon` and
`-std=gnu++98 -fpermissive` for this host compiler.

Build order:

```sh
cd /tmp/ax3292-toolchain
bash build-toolchain.sh binutils
bash build-toolchain.sh gcc
bash build-toolchain.sh newlib
bash build-toolchain.sh libgcc
python3 tests/validate.py
```

Use the compiler:

```sh
export PATH=/tmp/ax3292-toolchain/prefix/bin:$PATH
or1k-elf-gcc -mno-delay -mhard-div -Os -c example.c -o example.o
```

Always select `-mno-delay` for AX3292. LE is the patched default. The no-delay
runtime libraries are in:

- `prefix/lib/gcc/or1k-elf/4.9.1/no-delay/libgcc.a`
- `prefix/or1k-elf/lib/no-delay/{libc.a,libm.a}`

The installed multilibs cover delay/no-delay/compat-delay and explicit soft-float,
not both endiannesses. `-mbig-endian` works for standalone objects; it cannot use
these prebuilt LE runtimes. The observed default options are hard multiplication,
software division, and software floating point. `-mhard-div` selects hardware
division for SDK C code. Newlib was built with `-O2 -mhard-div`.

The build disables libgloss and newlib-supplied syscalls to avoid importing the
OpenRISC simulator board startup. Consequently, GCC's default bare-metal link
specs still name unavailable `crt0.o`, `libor1k`, and board libraries. SDK-style
manual `or1k-elf-ld` linking or explicit `-nostdlib` linking is required until a
real board/runtime port exists. The runtime fixture supplies a link-test-only
`_or1k_libc_getreent` hook returning `_impure_ptr`.

## Validation evidence

- `tests/assembler.elf`: exact LE instruction words, word/halfword/byte/string
  data, jump/call relocations, and HI16/LO16 address relocation.
- `tests/vendor-link.elf`: C-to-vendor-object link, resolved symbols, LE pointers,
  integers, 64-bit constants, floats/doubles, and bitfields; byte/halfword load and
  hardware divide instruction selection.
- `tests/runtime-link.elf`: resolved links for `memcpy`, `strcmp`, `sqrt`,
  `setjmp`, `longjmp`, `__adddf3`, and `__udivdi3`.
- All members of the tested no-delay libc, libm, and libgcc archives have ELF32
  little-endian OpenRISC metadata and flags `0x1`.
- Standalone BE C and assembly checks; negative tests reject mixed-endian and
  mixed-delay links.
- `tests/validation.json`, link maps/disassemblies, and `logs/validation.log`
  preserve the results. Section tests use symbol sizes to exclude alignment padding.

These checks do not establish runtime correctness, the complete vendor ABI,
64-bit argument/return conventions, interrupt behavior, PC+4 call/return behavior
on the actual CPU, or firmware compatibility. The C disassembly places the stack
restore immediately after `l.jal`, consistent with a PC+4 no-delay return.

## Full SDK probe and remaining blockers

`probe-sdk.py` reads the original CodeLite-generated makefile and compiles its 239
translation units into `tests/sdk-probe/`, without running packaging commands.

Result: **126 compiled, 113 failed**. The first errors divide into:

- 74 files: Windows case-insensitive include assumptions. Examples:
  `./maxlib/maxlib.h` versus the existing `Maxlib/maxlib.h`, and
  `uitipsRes.c`/`uitips1Res.c` versus `uiTipsRes.c`/`uiTips1Res.c`.
- 39 files: unsupported vendor keyword `__sfr`.

The SDK header `ax32xx/inc/ax329x.h` defines:

```c
#define SFR_AT *(volatile __sfr unsigned long*)
```

This needs compiler support for special-register accesses. Vendor `libsd.a`
contains `l.mfspr` and `l.mtspr`, including a write to immediate SPR `0x9888`.
That address matches the header's `SDBAUD` expression:
`(19 << 11) + (0x22 << 2) == 0x9888`.

Do not replace `__sfr` with an empty macro: generic `l.lwz`/`l.sw` operations
would access memory rather than the required SPRs. A named GCC address space
with SPR-specific load/store patterns is a candidate implementation, pending
validation of constant and computed SPR addresses, widths, casts, and compound
assignments. Further vendor extensions may appear after this blocker is fixed.

Preserved probe evidence: `tests/sdk-probe/report.json`, per-file logs,
`logs/sdk-probe.log`, and `tests/vendor-sd.disasm`.

Next: implement and test `__sfr`, resolve include case mismatches in an isolated
SDK copy, finish the SDK runtime port/link, and analyze AX3292 packaging. Hardware
validation and flashing remain deferred.
