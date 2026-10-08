# Reconstructed AX3292 toolchain

The KC02 runs an **AppoTech/Buildwin AX3292**: an OpenRISC 1000 derivative,
little-endian, **without delay slots**, with a grouped SPR layout that differs from
generic OpenRISC. No public toolchain builds for it, so this directory carries the
source patches that add support to a historical GCC/binutils/newlib set, together
with the results of validating the result against the device.

Only the patches and scripts live here. The sources, the build tree and the
installed compiler are not in git (the build tree is about 1.6 GB); rebuild them
from the pinned revisions below.

## Pinned revisions

| Component | Upstream | Commit |
| --- | --- | --- |
| GCC | <https://github.com/openrisc/or1k-gcc> | `50e11f73098c722676829ae73caf09246e81cd44` |
| binutils | <https://github.com/openrisc/binutils-gdb> | `68b975af7ef47a9d28f21f4c93431f35777a5109` |
| newlib | <https://github.com/openrisc/newlib> | `476cdfd8d4189ca00679273b2602c5951d0112c3` (`v2.3.0-1`) |

Historical pins are a reconstruction baseline, not proof of what the vendor used.
Matching versions and metadata do not establish that the vendor compiler had the
same patches.

## What the patches add

* **Native little-endian support** for the OpenRISC target, with no-delay and
  multilib support. The original fork was big-endian-only with delay slots; the
  vendor libraries and the firmware are little-endian and no-delay, so a
  byte-swapping build is not an option.
* **`__sfr`**, the vendor's special-function-register address space, implemented as
  GCC named address space 1 with dedicated `l.mfspr`/`l.mtspr` patterns. Ordinary
  RAM accesses are unaffected. SPR and RAM pointers are kept disjoint, computed
  selectors are formed with OR (not addition), and SPR stores conservatively clobber
  the condition flags. Only aligned 32-bit integer access is allowed. The SDK's
  `#define SFR_AT *(volatile __sfr unsigned long*)` and its
  `SDBAUD == (19 << 11) + (0x22 << 2) == 0x9888` both compile as expected.
* **Linker rejection of mixed-endian and mixed-delay objects**, which the original
  hook silently accepted.
* **AX3292 fixes to newlib `setjmp`/`longjmp`**: the status register moves from
  generic SPR `17` to the AX3292 `0x18`, `jmp_buf` grows from 31 to 32 words (the
  historical value let `setjmp` write four bytes past the buffer), and `longjmp`
  uses `l.sfeq` so a non-zero argument returns that value instead of 1.

Details, evidence and open questions are in [`SFR-RESULTS.md`](SFR-RESULTS.md)
(current) and [`RESULTS.md`](RESULTS.md) (historical baseline).

## Rebuilding

The build script expects the pinned sources under `src/{gcc,binutils,newlib}` and
prepared host dependencies under `host-deps/`; see the script header. It is not an
automatic downloader.

```sh
bash build-toolchain.sh binutils
bash build-toolchain.sh gcc
bash build-toolchain.sh newlib
bash build-toolchain.sh libgcc
```

Typical use:

```sh
or1k-elf-gcc -mno-delay -mhard-div -Os -c example.c -o example.o
```

Little-endian is the default and **`-mno-delay` is required**. Delay-slot and
compat-delay multilibs exist but do not match this SoC. `libgloss`/`syscalls` are
disabled: GCC's default spec refers to startup files this target does not install,
so link with `-nostdlib` and an explicit script, as the SDK does.

`export-patches.py` regenerates `patches/*.patch` from the edited sources and
verifies that applying each patch to a pristine copy reproduces them byte-for-byte.
`patches/manifest.json` records the file lists and patch hashes.

## Validation status

The compiler, ABI, `__sfr` addressing, memory layout, libc, `setjmp` runtime and
software floating point are verified **on the device**, not just offline: 160
profiles / 1200 vectors, in one uninterrupted session. See
[`../docs/TOOLCHAIN_TESTS.md`](../docs/TOOLCHAIN_TESTS.md) for the suite and
[`../docs/RAM_MAP_MEASURED.md`](../docs/RAM_MAP_MEASURED.md) for the RAM region it
executes in.

Not validated: interrupt entry and handler ABI, the complete vendor ABI, newlib
reentrancy/syscall compatibility, boot/startup configuration, and whether a rebuilt
SDK image is suitable for flashing.
