# Run compiler tests on a KC02 over USB

Use `tools/test_toolchain.py` to build AX3292 C payloads offline and run them as
RAM callbacks on the researched stock KC02 firmware. You do not need to flash a
firmware image. A faulty payload can hang the camera; keep a backup and a recovery
method, and power-cycle after a failed execution.

## Before connecting the camera

Build with the reconstructed little-endian, no-delay compiler. The default command
builds `-O0`, `-Os`, `-O2`, and `-O3` variants, fixed edge cases, and eight seeded
random vectors where applicable:

```sh
python3 tools/test_toolchain.py build \
  --tool-prefix /tmp/ax3292-toolchain/prefix/bin/or1k-elf-
python3 tools/test_toolchain.py check --model
python3 tools/test_toolchain.py list-tests
```

Run these commands from the repository root. The builder creates
`patched/toolchain-tests/build/`, which the existing `.gitignore` excludes.
The output directory must be empty; use `build --output PATH` for another build
and pass the same path as `--suite PATH` to subsequent commands. You can also
set `OR1K_TOOL_PREFIX` or put `or1k-elf-*` on `PATH`.

The builder does not use USB. It rejects unexpected imports, relocations,
endianness, delay flags, mutable static data, unapproved SPR accesses and image
overflow.
It also checks five invalid `__sfr` programs at each optimization level. The
instruction model executes the generated machine code and checks outputs, memory
bounds, stack restoration and registers. Its simulated SPI/cache routines and
assumed CPU semantics do not establish device or vendor-ABI compatibility.

With the currently reconstructed toolchain, the full build has:

- 160 profiles: 160 ready, none over the code budget, none blocked for safety.
- 20 compile-only rejection checks.
- 1200 compiled vectors passing the bounded instruction model.
- 104 repository regression tests passing, including the full fake-USB run and
  a stale-SPI instruction-cache regression.

Keep the build directory: it contains the exact source fingerprints, flags,
runtime hashes, ELF files, flat binaries, maps, disassemblies and logs. To run on
another computer, copy the complete build directory and the matching repository
scripts. The runner needs Python 3.9+, but not the compiler. Source changes require
a fresh build.

## Connect and run

1. Keep a verified `firmware/original.bin`, or pass `--reference PATH`. The runner
   validates its header and researched routine signatures before USB execution.
2. Remove any `DestBin.bin` from the SD card. Leave the camera idle on its USB/SD
   screen. Eject/unmount its volumes and close camera apps.
3. Install PyUSB and a libusb backend using the same Python:

   ```sh
   python3 -m pip install pyusb libusb-package
   ```

   Follow the existing [README](../README.md) for host permissions and interface
   driver setup. On Windows, substitute `py -3` for `python3`.
4. List the available cameras, then run:

   ```sh
   python3 tools/test_toolchain.py devices
   python3 tools/test_toolchain.py run --ack-ram-execution
   ```

   If more than one supported camera is present, add `--device NUMBER` using the
   current list. Numbering can change after reconnecting.

The runner checks live firmware signatures before RAM writes. It runs the existing
assembly canary and SPI adapter baseline, then each ready compiled profile, and
finishes with another canary. Each SPI test reads the first two aligned 2 KiB flash
chunks and compares them with your reference dump. Each RAM test checks its full
returned buffer, including program bytes, constants, inputs, fresh completion ID,
nonce, outputs and guards.

The runner does not send flash program/erase/update commands or reset the USB
device. It stops at the first failed result, upload readback, USB status or timeout,
and performs no automatic execution retry. A host timeout cannot stop a wedged CPU.
Stop and power-cycle before trying again.

Useful options:

```sh
# New output directory; an existing one is never overwritten.
python3 tools/test_toolchain.py run --ack-ram-execution --output patched/test-run-1

# Run one test family or one exact optimization variant; still runs baselines.
python3 tools/test_toolchain.py run --ack-ram-execution --only echo-O0
python3 tools/test_toolchain.py run --ack-ram-execution --only args1,preserved

# Repeat the entire ready suite, including baselines.
python3 tools/test_toolchain.py run --ack-ram-execution --repeat 3

# Diagnostic command/status logging; --trace also logs received bytes.
python3 tools/test_toolchain.py run --ack-ram-execution --verbose
```

## Read the results

By default the runner saves evidence under
`patched/toolchain-tests/runs/<UTC timestamp>/`:

- `run.json`: progress, passed/failed vectors, last command, blocked cases, errors.
- `suite.json`: build metadata and artifact fingerprints for that run.
- Per-vector `.bin` files: raw returned 2 KiB buffers.
- `canary.bin` and, on failure when available, `last-transfer.bin`.

Preserve both the evidence and original build directories. On a failure, inspect
the failing profile's `test.disasm` and the saved raw result before another run.

Exit codes:

| Code | Meaning |
| --- | --- |
| 0 | Selected ready profiles passed with no blocked profiles, or you accepted the runnable subset using `--allow-blocked` |
| 1 | Build, artifact, transport, output or correctness failure |
| 2 | Ready profiles passed, but the suite still has blocked profiles |
| 130 | Interrupted |

`--only` accepts comma-separated test names or profile keys. Reports mark their
scope as `selected` and list `not_selected` profiles: success is not a full-suite
pass. Blocks outside the selection are not counted in that run.

`--allow-blocked` changes only the final exit code. It does not execute a blocked
payload, bypass signatures or change the `INCOMPLETE` report into a full-suite pass.

## Hardware results (2026-10-08)

All **160 profiles / 1200 compiled vectors** have passing hardware evidence,
including the 33 that were previously over the old 1792-byte code budget. This covers all
four optimization levels where the payload fits: direct/indirect no-delay calls
and branches, register/SP preservation, 1–8 arguments, register/stack-passed
64-bit arguments, structure returns, memory/layout, arithmetic, `__sfr` reads,
libc memory functions, `setjmp`/`longjmp`, and the fitting soft-float helpers.
Stock SPI/cache interoperation passed on the tested paths.

The whole suite also passed in **one uninterrupted session**: 1202 vectors
(1200 compiled plus the two assembly-SPI baselines), nothing blocked, exit 0.
Evidence directory `patched/toolchain-tests/runs/20261008T221709.381163Z/`. Every
vector in the aggregate now comes from that run against the current build, so no
evidence is re-used from an earlier build.

The aggregate is `patched/toolchain-tests/hardware-validation.json`; earlier
aggregates are kept alongside it, and `single_session_runs` lists completed
whole-suite runs. Where an earlier build's evidence is used, a vector is re-used
only when that profile's executable code is byte-identical between that build and
the current one, and the run is re-tied to that build's payload hash; otherwise it
must come from a run whose source and runtime hashes match the current manifest.
Raw buffers are re-hashed from disk for every claimed vector, and nothing is
inferred for a profile whose code changed.

Re-use is not assumed harmless. The execution packet moved from `0x020D8100` to
`0x020D9700` between builds, so a payload that embedded its packet address would
have had to change; a profile whose code is byte-identical therefore cannot embed
one. That is why 11 profiles (`preserved-*`, whose oracle used a hardcoded output
cell, and `float_mul-*`/`float_div-*`, whose compact packet layout was dropped)
do not match and were re-executed.

### System-register map observed on hardware

The `spr_map` probe reads `SPR_VR`, `SPR_SR`, `SPR_PICVR` and `SPR_ICCR` three
ways: the C `__sfr` front end, the selector form (`l.mfspr rD,r0,K`) and the
register-base form (`l.mfspr rD,rA,0`). All three agree on `SPR_VR` (`0x65803200`,
exact), `SPR_SR` (configuration bits; only `SR[CY]` varies) and `SPR_PICVR`
(`0x00000000`, exact) at every optimization level. `SPR_PICVR` is in group 9
(`0x480c`) and `SPR_ICCR` in group 4 (`0x2000`), so this also confirms that the
5-bit group field is formed as expected, that the hardware computes
`SPR = (rA | K)`, and that the compiler's `__sfr` selector encoding matches the
register-base form the vendor compiler emits.

Observed `SPR_SR` was `0x0000c086`/`0x0000c486`: `FO` (bit 15), `EPH` (14),
`LEE` (7), `IEE` (2) and `TEE` (1) set, with `DCE` (3) and `ICE` (4) **clear**.
On this core the cache enables live in `SPR_ICCR`/`SPR_DCCR`, not in `SR`, so
restoring `SR` cannot disable caching. `SPR_ICCR` bit 0 (the enable bit the stock
routine manipulates) was set, while bits 8–9 changed between adjacent reads, so
`ICCR` is compared only on bit 0. That instability is why the probe does not
assert exact equality on it; it is recorded here as an observed property, not an
explained one.

### Run limitations

The aggregate includes validated records from sessions that later failed at a USB
upload, so those sessions did not finish their final canary. Later short batches
completed successfully, but this is **not one uninterrupted full-suite pass**.
Two USB disconnects remain unexplained, and the camera stopped enumerating once
before returning; a separate read-only 20-second check passed 970 reads with no
uploads or custom callbacks. The 33 size-blocked profiles were never executed.

The initial pre-fix SPI comparisons did not prove C execution: the next echo
returned stock flash bytes, consistent with a stale SPI shim. Instruction-cache
maintenance corrected this; echo code IDs/nonces/results and all subsequent
payload guards passed. The old build is preserved in
`patched/toolchain-tests/build-pre-icache/`.

No flash-write commands were sent and no full post-run flash readback was done.
These results do not validate interrupts, excluded runtime functions, all vendor
APIs, or a flashable SDK replacement.

## Coverage and exclusions

The implemented profiles test:

- Callback arguments, fresh program identity, direct/indirect PC+4 returns,
  branches, nested calls and function pointers. `tail_calls` enables the compiler's
  sibling-call option; its backend may still emit ordinary calls.
- Callee-saved registers, reserved thread/PIC registers, SP and an owned-frame guard.
- One through eight arguments, 64-bit argument/return word ordering, small and
  large structure returns, with independently assembled calling-convention oracles.
- Byte/halfword/word memory operations, signed extension, native LE structure and
  bitfield layout, integer arithmetic, hardware division, 64-bit operations.
- Immediate/computed `__sfr` reads of `SPR_VR`, and a read-only probe that
  compares the C front end, the selector form (`l.mfspr rD,r0,K`) and the
  register-base form (`l.mfspr rD,rA,0`) on `SPR_VR`, `SPR_SR` and `SPR_ICCR`;
  `SPR_ICCR` exercises the grouped encoding.
- Reconstructed newlib `memcpy`/`strcmp` and `setjmp`/`longjmp`; fitting soft-float
  operations and integer/float/double conversions, including signed zero.
- C-to-stock SPI interoperation. Additional stock APIs need vetted addresses and
  signatures before we can add them.

All profiles build. Code and the execution packet live in separate regions, so a
large code image no longer forces a large USB transfer: every transfer stays at
2048 bytes, the proven size. See the RAM layout section below for the adopted
addresses and [`RAM_MAP_MEASURED.md`](RAM_MAP_MEASURED.md) for the survey evidence
that justifies them.

The builder authorises system-register access per test case. Reads are permitted
with either addressing form, because the front end materialises even a constant
`__sfr` address into a register at `-O0` and `l.mfspr` has no side effects; a
statically encoded read address must still be on that case's reviewed list, so an
unexpected peripheral read is rejected. Writes are stricter: the address must be
statically encoded (an `r0` base) and on the reviewed write list, because a wrong
status, clock or cache write can disturb the running firmware. Only the `setjmp`
status-register save/restore at `0x18` is authorised to write.

The reconstructed newlib `setjmp`/`longjmp` was ported from generic OpenRISC to
the AX3292 before being enabled here. The pinned revision read and wrote legacy
SPR `0x11` (undefined on this core), its `jmp_buf` was one word too small for the
status-register slot at offset 124, and `longjmp` used `l.sfne` so a non-zero
result argument returned 1 and a zero argument returned 0. All three are fixed
and the fixes are exported in the toolchain patches.

This suite still excludes interrupt-entry tests and any CPU-status write other
than the reviewed `setjmp` restore.

C-to-assembly checks establish the tested candidate calling convention. Only the
stock SPI/cache calls exercise vendor interoperation. Passing these tests does not
prove the full vendor ABI, newlib reentrancy/syscall compatibility, interrupt
behavior or that a rebuilt SDK image is suitable for flashing.

## RAM layout and protocol reference

| Purpose | Address | Size |
| --- | --- | --- |
| Fixed dispatcher or C SPI adapter | `0x020D8000` | at most 128 bytes |
| Existing assembly canary | `0x020D8080` | 4 bytes |
| Executable test code/constants | `0x020D8100` | at most 5632 bytes |
| Execution packet (passed in `r3`, returned by the helper) | `0x020D9700` | 2048 bytes |
| — inputs | `0x020D9E00` | 64 bytes |
| — completion header | `0x020D9E40` | 32 bytes |
| — results | `0x020D9E60` | 144 bytes |
| — trailing guard | `0x020D9EF0` | 16 bytes |

The code image is linked at `0x020D8100` and uploaded in 2048-byte chunks; the
packet offsets are unchanged, because every payload derives them from the pointer
it receives. Two runs are separated by 84 bytes of slack below a live firmware
global at `0x020D9F54`. Each manifest pins the code limit, and the builder emits
literal linker limits. Code images are padded to 16 bytes so that a word-wide
libc read (newlib's `strcmp`) that runs a few bytes past the end of `.rodata`
stays inside bytes the device and the offline model both know.

The dispatcher is a fixed no-link `l.j` to `0x020D8100`. It preserves the stock
callback arguments and link register. The stock helper returns the 2048-byte
packet starting at the address passed to it. RAM payloads call the
signature-checked stock cache-clean helper on their aligned packet region before
returning, to publish CPU-written results. After every execution the runner
re-reads the shim, the canary and the whole code image, so stray writes into the
executed bytes are detected. Distinct compiled IDs detect stale instruction
execution across uploads; byte readback alone cannot prove it.

Data-cache writeback at `0x0202B058` does **not** invalidate instructions. Before
executing newly uploaded code, the runner calls the stock
`ax32xx_sysIcacheInit@0x0202B014` from unchanged firmware, never a replaceable RAM
stub. It pins all 68 bytes of that routine against both the reference and live
camera before RAM writes. The helper disables instruction caching, writes zero
to `SPR_ICAIR` (`0x2008`), and re-enables it, preserving other `SPR_ICCR` bits.
It leaves the instruction cache enabled, as the stock boot path does. The returned
packet must remain unchanged after maintenance. The transport rejects RAM
execution without this gate. This reviewed maintenance callback is the only added
stock cache-control operation; payloads still cannot write arbitrary SPRs.

Keep SPI adapters in the separate shim slot: a full 2 KiB SPI read into the payload
buffer would overwrite a test executing there. The test-specific transport owns
its whitelist and never changes the dumper's `RAM_BLOBS` or command guards.

`STAGING_LIMIT` is now `0x020D9F54`, the measured upper bound of the free span.
It is bookkeeping rather than a reservation: the dumper still accepts only the
fixed `RAM_BLOBS` writes, and the single-transfer size is still 2048 bytes. The
measured RAM map, the read-only survey tool and the adoption protocol are in
[`RAM_MAP_MEASURED.md`](RAM_MAP_MEASURED.md). That document also records why the
upper SDRAM (above `0x02103980`) must not be used: it is the firmware's own HAL
allocator pool.

## Offline regression tests

```sh
python3 -m unittest discover -s tests -v
OR1K_TEST_SUITE=patched/toolchain-tests/build \
  python3 -m unittest discover -s tests -v
```

The second command adds a complete fake-USB run using the instruction model,
artifact tampering checks and execution-acknowledgement checks. Neither command
opens a real USB device.
