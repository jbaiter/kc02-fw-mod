# HANDOVER — KC02 code RE: find a non-bricking flash→SD hook

## Latest findings: stock USB call path and SPI/file APIs

Read this section first. It supersedes the memory-model and API uncertainty in
older entries. Preserve the original flash and prefer a RAM-only investigation.
The historical promises of zero updater risk and execution from erased flash
have no evidential basis.

### Analysis model and reproducibility

- `FixOR1KMemory.java` now separates physical resources from provisional runtime
  RAM. Physical file bytes `0xd3200..0x3fffff` occupy the `physical_flash_tail`
  overlay at their physical offsets. Runtime globals/heap/stack occupy an
  uninitialized block `0x020d0c00..0x027fffff`.
- Boot code at file `0x528` sets SP to `0x027ffffc`. This supports the RAM model
  but does not prove available RAM, a safe staging address, or executable space.
- Application initialized data `0x0207b800..0x020d0bff` includes mutable globals
  such as `0x020c8a38`. The analysis marks the whole block writable to prevent
  false constant folding. The block name is `app_initialized_data`.
- `AnnotateOR1K.java` assigns observed register-based signatures to file, SPI,
  memory-copy and USB routines; it also creates the indirect-only entry at
  `0x0204c624`. It ran successfully against the cached project. The bootstrap
  now invokes it after the memory correction.
- **Remaining decompiler problem:** the inherited SLEIGH represents SPR access
  as indirect register-space loads/stores. The compiler spec declares global
  registers only through `0xffff`; peripheral SPR accesses resolve beyond this
  range. The decompiler drops hardware writes and sometimes arguments, and
  makes polling loops look invariant. Use instruction listings for peripheral
  and cache routines. Do not mistake a decompiled no-op for a hardware no-op.

### Confirmed stock USB mechanism

These findings come from this KC02 dump, not just the adjacent AX3295B report.

| CPU entry | File offset | Observed behavior |
|---|---|---|
| `0x0204ce08` | `0x04f408` | Parses a mass-storage CBW after checking `USBC` |
| `0x0204cbb8` | `0x04f1b8` | Dispatches SCSI opcodes, including vendor `0xcd` |
| `0x0204c660` | `0x04ec60` | Calls the packet-supplied function pointer |
| `0x0204c624` | `0x04ec24` | Selects stock memory read/write by CBW direction |
| `0x0204c52c` | `0x04eb2c` | Optional callback, then memory-to-USB transfer |
| `0x0204c5a4` | `0x04eba4` | USB-to-memory transfer, then optional callback |
| `0x0204c4a0` | `0x04eaa0` | Sends memory in chunks of at most `0x200` bytes |
| `0x0204c404` | `0x04ea04` | Receives memory in chunks of at most `0x200` bytes |

The CBW header occupies bytes `0..14`; CDB byte 0 is packet byte 15. The parser
stores these host-supplied fields:

| CDB bytes, inclusive | Encoding | Runtime destination / purpose |
|---|---|---|
| `0` | byte | `0x020da3e8`, SCSI opcode |
| `1..4` | little-endian 32-bit | `0x020da40c`, function address |
| `5..8` | little-endian 32-bit | `0x020da410`, transfer buffer address |
| `9..12` | little-endian 32-bit | `0x020da414`, optional callback address |
| `13..15` | little-endian 24-bit | `0x020da418`, callback argument |

At the vendor call, instructions prepare `r3=0x020da3d8` (parsed command context)
 and `r4=0x020da39c` (USB transport context), then call through `r5` loaded from
`0x020da40c`. This is not a general C-function call with CDB fields as arguments.
The stock transfer helper ignores those incoming arguments and uses globals.

On the read path, buffer value `0xffffffff` selects the runtime IN scratch
pointer from `0x020da3ac`. Callback value `0xffffffff` skips the callback;
zero does **not** skip it. Otherwise the helper prepares `r3=buffer`,
`r4=CBW transfer length`, `r5=24-bit callback argument` before the callback,
then sends the requested memory. The write path selects the OUT scratch pointer
at `0x020da3b0` and invokes its callback after receiving data.

The mechanism offers a possible route without a firmware update. USB descriptor
identity, scratch allocations, resource ownership, and usable staging/cache
handling remain to be checked. No device commands have been sent.

### Confirmed flash and file APIs

| CPU entry | Register arguments | Return / evidence |
|---|---|---|
| `0x0203d38c` | `r3=physical offset`, `r4=buffer`, `r5=count` | SPI READ `0x03`, three address bytes, then data; returns 0 |
| `0x0206c5b8` | `r3=path`, `r4=mode` (low byte) | Integer handle `0..3`, or -1; passes mode to FatFs |
| `0x0206c680` | `r3=handle` | Closes file and frees handle; 0 or -1 |
| `0x0206c6f8` | `r3=handle`, `r4=buffer`, `r5=count` | Actual bytes read, or -1 |
| `0x0206c770` | `r3=handle`, `r4=buffer`, `r5=count` | Actual bytes written, or -1 |
| `0x0206c7e8` | `r3=handle`, `r4=absolute offset` | Offset on success; negative errors; no whence argument |

The write wrapper calls the FatFs-like routine at `0x0207174c` with four
arguments `(file_object, buffer, count, &bytes_written)`. Its implementation
writes sectors, updates the file position/size, and marks cached data dirty.
This validates the previously tentative SD-write entry. Short writes still
need checking in any later backup routine.

SPI reads below 16 bytes use a byte loop. Larger reads call `0x0203d198`, which
rounds the DMA length **up** to a multiple of 16. A destination must accommodate
that rounded length. This API reads physical flash, including header and assets;
reading four megabytes from application RAM would not reproduce the flash dump.

The USB read callback order `(buffer, length, argument)` does not match SPI read
`(offset, buffer, count)`. Do not call SPI read as that callback without an
adapter or a verified existing wrapper. No adapter or device payload exists yet.

### Completed delegated investigation and parent review

Read-only analyst run `deb5a0d6-60da-4fb6-a160-7b53df0e5045` used
`deepseek/deepseek-flash`. Mission `db3d683f-544c-4761-a5ad-00b3e3cc9a49`.
Report: `/home/jbaiter/.pi/agent/sessions/--home-jbaiter-Downloads--/subagent-artifacts/outputs/deb5a0d6-60da-4fb6-a160-7b53df0e5045/kc02-usb-spi-analysis.md`.

- Init `0x0204bbd4` stores fixed USB scratch pointers: IN `0x00008040`, OUT
  `0x00008250`, with 512-byte transfer chunks and endpoint 1. Instructions
  `0x0204bc2c..0x0204bc5c` show constants rather than heap allocations.
  The 0x210 spacing alone does not prove all intervening bytes are spare.
- Wrappers `0x0204a698` and `0x0204a6c4` preserve `r4=buffer`, mask
  `r3=endpoint` to 8 bits and `r5=length` to 16 bits, and set `r6=0x200`.
  They call `0x02031410` (IN) and `0x0203154c` (OUT).
- Parent raw-byte review confirms the config at `0x0208297c`: one interface,
  literal interface number **4**, class/subclass/protocol `08/06/50`, bulk
  endpoints `0x81` IN and `0x01` OUT, maximum packet 512 bytes. Do not assume
  interface number 0. The standalone count/number combination is unusual.
- Device descriptors: `0x020829c4` = VID:PID `0219:3280`, `0x020829b0` =
  `1908:3282`, `0x0208299c` = `1908:3283`. Parent review of selector
  `0x0204badc` confirms mode 0 pairs `0219:3280` with the mass-storage config;
  mode 1 uses the composite config `0x020827a4`; mode 2 chooses `0x020825b4`
  or `0x020823c4`. The camera's active mode still needs runtime confirmation.
- Data-cache candidates: `0x0202b058` writes SPR `0x180c` (clean), while
  `0x0202b0d4` writes `0x1814` (invalidate or clean+invalidate). Both use
  16-byte lines and skip addresses below `0x44000`. DMA-copy caller
  `0x0202b568` calls the first on its source and the second on its destination.
  Exact hardware semantics remain inferred. No instruction-cache helper has
  yet been identified.
- Analyst found no compatible stock flash-read adapter within the inspected
  functions. This is not proof that none exists. The raw SPI read signature
  differs from both the vendor-call context arguments and the optional
  callback signature; direct substitution would use incorrect addresses and
  lengths. A verified adapter and execution/cache arrangement remain open.

### 2026-10-01 second pass: flash-window verdict, shim, cache, SAFETY

DeepSeek analyst run `c0eb4dcb-b18c-4a5a-99aa-d71341fe797b`, mission
`4b890938-b52c-44e4-beeb-c3e0481ac079`. Report:
`…/subagent-artifacts/outputs/c0eb4dcb-b18c-4a5a-99aa-d71341fe797b/kc02-adapter-cache-report.md`.
Parent re-verified the two load-bearing claims from raw disassembly.

**Flash is SPI-only; it is NOT mapped into CPU address space (parent-confirmed).**
Evidence: (1) the updater's flash read-back verify calls SPI read `0x0203d38c`
into RAM and compares in RAM (`0x020026f8..0x0200271c`), never a mapped load;
(2) the only flash-header read (`BLDR`) is `SPI_read(4,&stack,4)` at
`0x0205b298..0x0205b2b8`; (3) every flash-content access in the image goes
through the SPI controller; (4) a linear flash window would collide with the
boot-time RAM zero-fill `0x020c869c..0x027fec00` (flash offsets `0xCAE00`,
`0xCD9B0`, `0xCDA40` fall inside it). **Consequence: the USB arbitrary-memory-
read primitive cannot fetch flash directly.** This supersedes the earlier
"full-chip XIP not established" note: it is now established as NOT mapped.

**A ~6-instruction shim is required (no stock match).** The optional callback
convention is `fn(r3=buffer,r4=count,r5=arg)`; SPI read is
`fn(r3=offset,r4=buffer,r5=count)`. All 29 call sites of `0x0203d38c` use
`(r3=offset,r4=dest,r5=count)`; none is `(dest,count,offset)`. The only
callback-shaped stock callee is `0x0203d198` `(r3=buf,r4=count)`, which does no
opcode/address/CS phase, so it cannot target a flash offset. Minimal shim:
`mov r2,r3; mov r3,r5; mov r5,r4; mov r4,r2; jal 0x0203d38c; jr r9` (r6 spare).
Direct `callback = 0x0203d38c` is a dead end: the post-call send re-reads the
buffer field (= the flash offset) as the source, requiring `F==N==D`.

**Stock USB debug primitive (parent-confirmed at `0x0204c624`/`0x0204c52c`).**
Vendor `0xcd` -> `0x0204c660` -> `fn(&cmd_ctx 0x020da3d8, &xport_ctx
0x020da39c)` through `cmd_ctx+0x34` (CDB[1..4]). Pointing `fn` at `0x0204c624`
gives arbitrary CPU-memory read (`cmd_ctx+8=0x80`) or write (`!=0x80`) at
`cmd_ctx+0x38` (CDB[5..8]), length `cmd_ctx+0x24`, with **callback
`cmd_ctx+0x3c` = `0xffffffff` to skip it** (a value of `0` does NOT skip and
would call address 0). Verified in `0x0204c52c`: buffer `-1` selects the
`0x8040` scratch at `xport_ctx+0x10`; the callback gets
`(r3=buffer,r4=length,r5=arg)`, then the send re-reads `cmd_ctx+0x38`.
Reachability of `0x0204c624` via the host pointer slot is the one open item —
it is not a literal/table target, only the CDB field materializes it.

### ⚠ SAFETY — the one real brick vector over USB

The `cmd_ctx+0x34` function-pointer slot and the `cmd_ctx+0x3c` callback slot
call **arbitrary addresses**. These flash-modifying routines must NEVER be
pointed at accidentally:

| CPU | SPI opcode | Effect |
|---|---|---|
| `0x0203d584` | `0x20` | **4 KB SECTOR ERASE** |
| `0x0203d454` / `0x0203d4e8` | `0x02` | **PAGE PROGRAM** (writes flash) |
| `0x0203d25c` | `0x06` | WREN (write-enable latch; alone is transient) |
| `0x02002418` | — | the `DestBin.bin` upgrade task (erase + program) |

A bad pointer that lands on a SECTOR ERASE / PAGE PROGRAM / the upgrade task
corrupts firmware = the brick case. Reading is safe; writing/erasing is not.
Keep every function/callback pointer confined to `0x0203d38c` (SPI READ) and
the shim. Do not probe by calling random addresses.

**Cache (sub-goal B).** Data-cache maintenance exists and is usable:
`0x0202b058` (SPR `0x180c`) = **clean**, `0x0202b0d4` (SPR `0x1814`) =
**invalidate (clean component not excluded)**, 16-byte lines, range
`0x44000..0x7fffffff`, cap 0x1000/chunk. DMA copy `0x0202b568` cleans source
(`0x180c`) and invalidates destination (`0x1814`). **No I-cache
range-invalidate helper exists** (candidates `SPR 0x1800/0x1808/0x1810` are
unproven; `0x1810` is only ever written `0xFFFFFFFF`). RAM executability is
also unverified (only `app_text` is marked executable). **So a RAM-written shim
has TWO open hardware questions: is that RAM executable, and what I-cache
invalidate is needed before fetching it.** Both are power-cycle-recoverable if
wrong (RAM clears), not bricking — as long as no flash write happens.

**One empirical falsifier for the window (device interaction, operator call).**
Read CPU `0x01ffda04` (4 bytes) via the free primitive with callback skipped.
If it returns `42 4C 44 52` (`BLDR`), a flash window exists at `0x01ffda00` and
the whole dump needs **no shim**. Caveat: an unmapped read may raise a bus error
(`or1k_bus_error` @ `0x00000200`) and leave USB state unknown — power-cycle
recoverable, but it is a device-side risk decision, not a routine probe.

**Do NOT drive `0x02002418` (`DestBin.bin` upgrade)** — it sector-erases and
page-programs flash. Read-only backup paths only.

### 2026-10-01 third pass: WebUSB feasibility + live-asset-injection targets

DeepSeek analyst run `4dda631c-4a99-451d-9ee0-2a9f1f942759`, mission
`c8930ecd-cfd2-41bd-afb6-b3d98c715ff8`. Report:
`…/subagent-artifacts/outputs/4dda631c-4a99-451d-9ee0-2a9f1f942759/kc02-usb-webusb-livepatch-feasibility.md`.
Parent re-verified the Q1 negative with raw byte searches. This section is for
a WebUSB/webapp live-asset-patching tool (runtime RAM injection to restyle UI
without flashing).

**USB interface reality (parent-verified).** Raw searches: class-FF interface
(`09 04 .. FF`) = 0 hits; class-FE = 0 hits; mass-storage (`09 04 .. 08 06 50`)
= 3 hits (`0x02082985` mode0, `0x0208259a` mode2C, `0x0208278a` mode2D). **No
vendor interface, no WebUSB BOS capability anywhere.** Config A (mode 0,
`0x0208297c`) is literally one interface, **bInterfaceNumber 4**, class
`08/06/50`, bulk `0x81` IN / `0x01` OUT, max-pkt 512. Config B (mode 1,
`0x020827a4`) = 4 interfaces, all UVC (0E/01,0E/02) + UAC (01/01,01/02),
**no bulk endpoints at all**. Configs C/D (mode 2) = UVC+UAC + interface 4
mass-storage (bulk 0x81/0x01). **Conclusion: WebUSB has nothing clean to claim**
— it would have to take the mass-storage interface from `usb-storage` (Linux
udev gymnastics, Linux-only, and no BOS cap means re-pick device per session).
**Recommended architecture: native libusb shim owns USB (auto-detach
`usb-storage`), webapp is a UI over WebSocket.**

**Mode select + primitive reachability.** `FUN_0204badc(r3&0xff)` fills the
descriptor record in writable RAM `0x020d1bd4` (dev_desc ptr, 18, config ptr,
wTotalLength). Mode 0 -> dev `0x020829c4` (0219:3280)+config `0x0208297c`;
mode 1 -> `0x020829b0` (1908:3282)+`0x020827a4`; mode 2 -> `0x0208299c`
(1908:3283)+`0x020825b4`/`0x020823c4` (picked by
`*(u32*)(*(0x020d1e84)+0x18)==0x500`). Mode is chosen at (re)enumeration by a
hardware cable/ADC sense state machine (`FUN_0200c8d0`, reads sense regs
`0x20000002`/`0x20000003` via `0x02002a4c`, state bytes `0x020d416c..0x020d416e`,
re-enum via `0x02021648`) — **NOT SCSI-driven, NOT SD-driven.** The confirmed
default is **mode 0** = plain MSD = where the `0xcd` primitive lives. The
`0xcd`/memory-transfer primitives are wire-reachable ONLY in mode 0 and mode 2
(interface 4 bulk 0x81/0x01); mode 1 has no bulk so they are unreachable there.
The BOT dispatcher `0x0204cbb8` is not code-gated by mode — only by whether the
active config has a bulk endpoint.

**Live asset-injection targets (for the webapp).** SFAT asset dir at flash
`0x0D3200`, 8 B/record at `0x0D3200 + 8*(n+1)` = (LE u32 rel_offset, LE u32
size), abs = `0x0D3200 + rel`. Menu art: background JPEG #31 at file `0x1A6102`
(41,106 B, confirmed `FF D8 FF E0 .. JFIF`); music icon #30 `0x19F4CA`;
menu icons `0x1B0194/0x1B6DCC/0x1BDA04/0x1C463C/0x1CB274` (27,704 B each);
frame JPEG #0 `0x0D3510` (26,226 B). Loader `FUN_02046860(dst, selector)`
calls SPI `0x0203d38c` then `0x0202fe14(dst)` (post-load cache op). **RAM
staging buffers (the live-injection targets):** `0x020e32a0` (screen background,
from flash `*(0x020e4ba0)+0x0CD9B0`), `0x020d1ea0` (secondary), `0x020d7520`,
`0x020ce260`; also `0x020d7420` (6x256 B menu labels from `0x0CDA40+idx*0x100`).
The panel feed loops (`FUN_0204a9fc`, `FUN_0204fd9c`) consume the **compressed
bytes from RAM** and test JPEG SOI `FF D8` at `0x0204aa80` — so overwriting a
staging buffer changes the displayed image. **Caveat:** buffers refill from SPI
at screen entry (`FUN_02046cf0` calls the loaders at `0x02046d3c`), so a RAM
patch is transient until the next reload unless re-applied. Asset bytes remain
authoritative in SPI flash; persistence still needs an SPI/SD write. A runtime
heap canvas is pointed to by `0x020d2efc` (geometry u16 at `0x020d2ef4`/`+6`,
allocated `w*h` bytes via `0x0203e5c4`) — dynamic address, read it live.

**Display + cache sync (for injection correctness).** LCD layer A regs = SPR
`0x8900/0x8904/0x8908/0x890C`; layer B = `0x8910/0x8914/0x8918/0x891C`.
Completion is **polled** (SPR `0x8910` bit1, SPR `0xA048` bit4); no vsync IRQ.
After a CPU write to a buffer the LCDC will DMA, a **D-cache clean `0x0202b058`
(SPR 0x180C) is mandatory** (the stock driver does it at `0x0202b5b0`,
`0x0202b6b8`). Data injection needs only D-cache maintenance; **code** injection
still has the open I-cache-coherency problem (no range-invalidate helper).

**Menu-table caveat.** The six values at `0x0207bb78` (34,37,31,35,33,36) are
NOT proven SFAT indices (31 is the background JPEG, 37 is font data); they are
likely internal resource indices. No direct code xref to `0x0207bb60`/`+0x0207bb78`
— reached indirectly via `&0x0207bb90` stored at `0x020c4e1c`, passed at
`0x02000084`. Resolve before patching that table.

### 2026-10-01 fourth pass: shim route BUILT + parent-verified

Builder run `860dca61-23a3-4bbd-960a-2ff138b8cfef`, mission
`d4c76481-c820-473a-9013-9b013cc91195`. Tooling under `tools/`:
`kc02_shim.py` (hand-assembled shim+canary+encoders), `kc02_usb.py` (extended
with `load-shim`/`canary`/`flash-read`/`shim-info`/`selftest`),
`verify_shim_ghidra.sh`, `DumpShim.java`, `shim_roundtrip.txt`,
`canary_roundtrip.txt`. All offline; no device opened; firmware md5 and the
main Ghidra cache unmodified.

**Shim (12 instr / 48 B, load 0x020D8000).** Parent re-verified byte-for-byte:
save-first idiom `b.sw -8(r1),r2 / b.sw -4(r1),r9 / b.addi r1,r1,-8`, then
shuffle `r2=dest,r3=offset,r5=count,r4=dest`, `l.jal 0x0203d38c` from
0x020D801C, epilogue `b.addi r1,r1,+8 / lwz r2 / lwz r9 / l.jr r9`. Encodings
are harvested from stock bytes (mov=`b.ori rD,rA,0`, `enc_ori` layout
byte2=(rd&7)<<5|(ra&0x1f), byte3=0xA8|((rd>>3)&3)); `l.jal` formula
`word=0x04000000|((target-inst)>>2)` reproduces stock `3588ff07`@0x0205B2B8.
**Stack frame is correct (parent-confirmed against SPI_read 0x0203D38C
prologue: it saves r9@-4,r2@-0x14,r1@-0x18 then `addi r1,r1,-0x18`, i.e. it
only writes below its entry SP = the shim's post-alloc SP, so the shim's saves
at entry-8/-4 sit ABOVE it — no collision).** NOTE: an earlier intermediate
shim allocated -16 FIRST then saved below SP (a real crash bug); the delivered
version fixed this to save-first. Canary 0x020D8080 = `l.jr r9` (4 B). Data
buffer 0x020D8100 (512 B). Scratch window 0x020D8000..0x020D8300, inside
0x02044000..0x027FEC00, clear of all known-used RAM.

**Tool safety (parent-verified).** `selftest` passes offline: fn slot pinned to
0x0204C624 (memory transfer); callback slot only 0xFFFFFFFF (skip), 0x0202B058
(D-cache clean), or the loaded shim/canary; all writes range-bounded to the
scratch window; forbidden routines 0x0203D584/0x0203D454/0x0203D4E8/0x0203D25C/
0x02002418 unreachable. flash-read/canary/read never write flash.

**How to dump flash over USB (offline → device):** `probe` first (BLDR at
0x01FFDA04 = flash is mapped → just use `read` over the window 0x01FFDA00+off).
If NOT mapped: `canary` (proves RAM-exec + I-cache; hang ⇒ power-cycle, nothing
in flash touched), then `flash-read --offset F --len N --out f.bin` (512-byte
chunks via the shim at 0x020D8000). Full 4 MB = `flash-read --offset 0 --len
0x400000`.

**Residual risks (open, all power-cycle-recoverable):** (1) I-cache coherency —
no stock range-invalidate; relies on 0x020D8000 being never-fetched RAM; (2) RAM
executability — canary is the falsifier; (3) scratch occupancy if firmware
allocates 0x020D8000..0x020D8300 at runtime (read-back check in load_scratch
mitigates); (4) SPI DMA rounds count up to a multiple of 16 → up to 15 bytes of
flash over-read past the end (harmless, read-only; 512-byte chunks avoid it);
(5) write-path callback ABI + cache-clean-on-write unexercised on hardware.

### 2026-10-01 fifth pass: 0xCD is reachable in NORMAL operation (verified)

Q: does the 0xCD vendor command need a special mode/unlock first? **NO.**
Instruction-level static analysis, no gate anywhere in the command path:

| Layer | Finding |
|---|---|
| CBW parser `0x0204ce08` | only checks `USBC` sig (bytes 0-3, `b.sfnei`/`b.bf` per byte); stores opcode `CDB[0]`→`cmd_ctx+0x10` UNFILTERED, fn-ptr `CDB[1..4]`→`cmd_ctx+0x34`. No opcode/LUN/auth check. |
| receive poll `0x0204d014` | reads USB status SPRs `0xa058` bit0 / `0xa064` / `0xa060`; = "is a CBW waiting?" only. No gate. |
| wrapper `0x0204d0a8` | poll → parse → dispatch, each unconditional on the prior succeeding. |
| dispatcher `0x0204cbb8` | **0xCD handled in ALL THREE branches**, inline with standard SCSI: zero-len `b.sfnei r4,0xcd`/`b.bnf 0x0204cd3c`→`l.jal 0x0204c660`; IN (dir 0x80) after 0x28/0x03/0x12/0x1a/0x23/0x25 handlers `b.sfnei r4,0xcd`/`b.bf`→`l.jal 0x0204c660`@`0x0204cd3c`; OUT `b.sfnei r4,0xcd`→`l.jal 0x0204c660`@`0x0204cdc0`. Sits right beside READ(10)=0x28, WRITE(10)=0x2a, INQUIRY=0x12, READ CAPACITY=0x25. |
| vendor handler `0x0204c660` | sets `cmd_ctx+0x24`=transfer_length, `r3=&cmd_ctx r4=&xport_ctx`, `r5=[cmd_ctx+0x34]`, `l.jalr r5` — **calls the host-supplied pointer with ZERO validation**, then clears `cmd_ctx+0x24`. |

**Logical clincher:** 0xCD rides the IDENTICAL service (`0x0204abc8` → `0x0204d0a8`
→ `0x0204cbb8`) as READ(10)/INQUIRY/READ CAPACITY. If standard SCSI works (it
must, for the drive to function), 0_CD is dispatched the same way. No separate
vendor/debug mode exists.

**The ONLY real requirement is the USB config exposing bulk endpoints** (0_CD
rides the MSD bulk pair 0x81/0x01). Mode state machine `FUN_0200c8d0`: on
connection detect (sense reg `0x20000002` != 0, state 0→1) it calls
**`FUN_0204a964(0)` = MODE 0 (pure MSD, 0219:3280)** then re-enumerates. So the
plug-in DEFAULT is exactly the right mode. Mode 2 (UVC+MSD composite 1908:3283)
also has bulk → works. Mode 1 (webcam-only 1908:3282) has NO bulk endpoints at
all → no BOT traffic → 0_CD unreachable (the one state to avoid).

**Practical test before the 0_CD session:** `lsusb` should show `0219:3280`
(mode 0) or `1908:3283` (mode 2). If it shows `1908:3282` it is in webcam-only
mode 1 — re-plug / get back to the storage identity first. No menu dance, unlock
command, or special firmware state is needed for 0_CD itself.

### 2026-10-01 sixth pass: USB lock screen located + RAM-only unlock patch built

Q: why does plugging USB freeze the camera on an SD-card screen with dead
buttons, and can we unlock it in RAM without touching the USB/MSC transport?

**The lock is application mode 10, "usb device".** Mechanism, from listings:

| Step | Address | What happens |
|---|---|---|
| USB connect | `FUN_0200c8d0` @ `0x0200c8d0`, driven by `FUN_0200d2c0` (10-tick task) | sense reg `0x20000002` != 0 -> `0x020d416e` 0->1, `FUN_0204a964(0)` = mode 0 MSD (0219:3280), posts event **type 2, value 1** |
| host drives BOT | same fn, `FUN_0204accc()` != 0 | `0x020d416e` 1->2, posts event **type 2, value 2** |
| event 2 hook | `FUN_02007ee4` (registered by `FUN_02007938(0x020c2a64)`) | if `0x020d416e == 2` -> `FUN_02008108(10,1)` = *request app mode 10* (`0x02007f54` imm `0x0a`, call at `0x02007f5c`) |
| boot path | `FUN_0200046c` @ `0x02000494` | if `0x020d416e == 2` requests mode 10, else mode 6 (Main menu) |
| mode 10 record | `0x020c339c` (name `0x0207ba8b` "usb device") | enter `0x0200d498`, exit `0x0200d450`, frame `0x0200d588` |
| enter | `0x0200d498` | `DAT_020ccd8c=1`, `FUN_020041b0()`, `FUN_02004944(7)` (renders the SD/USB screen), `FUN_02006f88(0x60/0x61)`, `FUN_0204a964(0 or 1)`, then `FUN_02007880(0x020c3354)` = install the mode-10 key hooks |
| key hooks | list `0x020c3354` -> handlers `0x0200d3d8/0x0200d3f0/0x0200d408/0x0200d420/0x0200d438` | **all are `return 0` stubs** (0x0200d550 and 0x0200d5e4 are the only live ones) -> every button is swallowed |

**Why the camera cannot simply "use another mode":** the BOT/SCSI processor
`FUN_0204d0a8` is reachable through exactly one path `FUN_0204abc8 ->
FUN_0204d0a8`, and `FUN_0204abc8` has exactly **one** caller: mode 10's frame
`0x0200d598`. Stock code services the mass-storage transport *only* while the
USB screen is the active mode. Mode 10's exit `0x0200d450` -> `FUN_0204a9a4` ->
`FUN_0202ad98(0xf,0)` powers the USB interface **down**, and its enter
`FUN_0204a964` powers it **up**.

**Mode records / writability.** Mode table = `DAT_020ccd5c` (12 pointers,
`0x020ccd5c`) built at boot by `FUN_02008070` from `FUN_02000000`. Mode 6
(Main menu, `0x020c4c18`) has enter `0x020130e4` (reset layers, start the menu
object `0x020c4c2c` via `FUN_02012304`, which installs the menu key hooks) and
frame `0x020130bc` (**a no-op stub**). Code and the records themselves are XIP
flash and read-only to the 0xCD transport, but the mode table + the event-hook
tables (`0x020ccc8c`, `0x020ccbd4`) are RAM, so the patch rewrites *pointers*.

**Delivered patch** (`python3 tools/kc02_usb.py unlock-usb`, definition in
`tools/kc02_unlock.py`) - five bounded RAM writes, no flash, no code patch:

| Label | Address | Bytes |
|---|---|---|
| frame stub | `0x020D9000` | `fc4fe1d7f8ff219cf0c6fd072ce8fc070800219cfcff218500480044` (28 B: `sw -4(r1),r9; addi r1,-8; jal 0x0204abc8; jal 0x020130bc; addi r1,+8; lwz r9,-4(r1); jr r9`) |
| mode-10 record | `0x020D9100` | 0x100-byte copy of the stock Main-menu record, with `+0x0c` (exit) = 0 and `+0x10` (frame) = `0x020D9000` |
| mode table[10] | `0x020CCD84` | `00910d02` (= `0x020D9100`) |
| active rec slot | `0x020CCD58` | `00910d02` (so the mode machine's "exit current record" step runs our exit = 0 and never calls `0x0200d450`) |
| pending mode | `0x020CCD50` | `0a000000` (re-enter mode 10 through the patched record) |

Result: mode 10 becomes "Main menu UI + BOT serviced every frame"; the USB
interface is never powered down; the SD screen never renders; the Main-menu key
hooks are installed by the stock enter; re-locking is impossible because mode 10
*is* the patched record. Power-cycle reverts everything.

New/updated tooling: `tools/kc02_unlock.py` (patch definition + selftest),
`tools/kc02_disasm_region.sh` (throwaway-cache disassembly of any code region),
`tools/verify_unlock_ghidra.sh` (Ghidra round-trip of the stub),
`tools/kc02_usb.py` gained `poke` (RAM 0x020c0000..0x027FEC00 only) and
`unlock-usb`. All offline: no device opened, firmware md5 and the main Ghidra
cache untouched.

## Update, 2026-10-01: OpenRISC decode and corrected app mapping

**The ISA and mapping claims below are superseded. Do not build a payload from
those claims.** Read-only analysis now gives coherent little-endian, fixed-width
OpenRISC-compatible instructions. The exact SoC identity remains unconfirmed.

- App mapping: `CPU = 0x02000000 + file_offset - 0x2600`. Six independent split
  `movhi`/`ori` references resolve DestBin.bin, SELFTEST.bin, version, FAT diagnostics,
  menu coordinates and menu assets with this formula.
- First six dispatch pointers do land on function prologues with this mapping.
  Their UI meanings remain unverified. Invalid pi32 decoding was not evidence
  that the pointers were invalid.
- No-delay-slot semantics fit branch-to-branch sequences and returns followed
  by another function's prologue. Scalar results use `r11`; stack is `r1`, link
  is `r9`, and arguments begin at `r3`. Full ABI preservation rules remain provisional.
- Ghidra language `KC02:LE:32:nodelay` now imports and decompiles the app:
  1663 candidate functions, seeded from direct calls and six dispatch pointers.
  Sources: `reference/ghidra-kc02-or1k/`, adapted from CUB3D/Ghidra-Aeon commit
  `db212abfe3a86a167b83fa38bd3ed3c9c5036c55` (Apache-2.0).
- New bootstrap: `ghidra_kc02/BootstrapOR1K.java`; raw import base `0x01ffda00`.
  Cache for the `ghidra` tool: `/home/jbaiter/pi-ghidra-cache-kc02-or1k`.
  Project: `artifacts/d837019dd7de37ecda89a86399f01060e1a265c48bbc5a4befdf0bc7bb03f19e/project/pi-ghidra.gpr`.
  Ghidra rejects a project path containing `.src`; keep the cache outside it.
- Updater entry: CPU `0x02002418`, file `0x004A18`. Decompilation shows
  opening DestBin.bin, erase/write, then readback comparison and failure UI.
  Integer-handle file API candidates: open `0x0206C5B8`, close `0x0206C680`,
  read `0x0206C6F8`, seek `0x0206C7E8`, size `0x0206CAF8`;
  likely write `0x0206C770` still needs validation.
- **Full-chip XIP is NOT established.** The app writes globals at `0x020D334C`
  and `0x020D60C0`, which overlap SFAT/assets/JPEG bytes in a naively mapped dump.
  The app may be copied into RAM at `0x02000000`, or use a limited flash window.
  The current Ghidra bootstrap wrongly maps the physical asset tail as read-only
  runtime memory; its decompiler can fold JPEG bytes into bogus global constants
  and remove reachable branches. Fix the RAM/asset separation before trusting
  those expressions or deciding whether free flash can host executable code.
- Physical flash `0x307200` would correspond to CPU `0x02304C00` only IF this
  mapping extends there. Neither execution there nor a contiguous mapped flash
  dump source has been verified. A raw SPI-read API may be necessary.
- Original dump remains unchanged; no payload or update image has been built.
  Researcher run `afb51213-299b-4e7d-bc4c-9308ead3714f` is investigating existing
  JieLi/OpenRISC tooling and boot/memory mapping. Mission:
  `8b949748-ffb0-42cf-80cd-0223cab72e14`.

---

Compiled 2026-10-01. Target model: **HiMont KC02 (U8) kids instant-print camera**.
Written for the next model/agent picking this up. Read `docs/FIRMWARE_NOTES.md`
and `README.md` in this repo first — they contain the original author's asset/UI
findings, but **treat every function address in them as unverified** (see
"The blocker" below).

---

## 1. Mission

Get the KC02 to write its own 4 MB SPI flash to a file on the SD card, with
**zero brick risk**. This is the first step toward arbitrary code execution
(theme/UI replacement is already solved; code mods are not).

Hard rules from the operator:
- **Do not risk bricking.** Recovery requires desoldering the SOP8 flash
  (the SOIC8 clip read is too noisy on this board — confirmed by the operator
  and by GainSec's writeup of the same camera class).
- Payload must run from the **erased free region**, never overwrite real code.
- Prefer a **non-fatal hook** so a bad payload cannot hang the boot.

Delivery mechanism is already proven: the camera's own SD update
(`DestBin.bin` on the SD root → "Upgrading Firmware"), and the firmware has
**no CRC/hash/signature** on the image. See `docs/FIRMWARE_NOTES.md`.

---

## 2. Verified facts (re-checked this session)

### The dump
- Path: `/home/jbaiter/Downloads/original_firmware_backup.bin`
- Size 4,194,304 (0x400000), md5 `7236c4c1bd3f12b1d85d33a9bebdf510`
- Valid: 8-bit header checksum over first 512 bytes = 0; `BLDR`@0x04;
  `0x55AA`@0x1FE; `SFAT`@0x304; assets end 0x3071FF; 0x307200–0x3FFFFF all 0xFF.
- Anchor tables all match the repo:
  - `menu_coord_table`@0x07E160 = (12,18),(112,18),(212,18),(12,126),(112,126),(212,126)
  - `menu_asset_table`@0x07E178 = [34,37,31,35,33,36]
  - `ui_state_dispatch`@0x07DF08 first 6 = 0x0200411C,0x02003E74,0x02003EF0,0x02003F90,0x02004014,0x02004098
  - `init_callback_table`@0x07E000 = 0x020093AC,0x020093B4,… (10 entries)
  - strings: `DestBin.bin`@0x07DE87, `upgrade failed!!!`@0x07DE93, `upgrade`@0x07DEA5,
    `exmend.bin`@0x07DF94, `329X_V1.0.0`@0x07DF9F, `JRX_20220309`@0x07DFD8, `SELFTEST.bin`@0x07DFE5
- XIP mapping: **flash file offset + 0x02000000 = CPU address** (verified on the
  data side: assets at their documented offsets are valid JPEGs, string data is
  plaintext, tables contain valid in-range pointers).

### Payload staging area
- Free/erased: file `0x307200…0x3FFFFF`  → XIP `0x02307200…0x023FFFFF`.
- ~1.1 MB available, no code or assets there. Perfect for a payload.

### The delivery path
- `DestBin.bin` on SD (FAT32 root) → firmware flashes it. Proven by the repo's
  Flash #1..#19 experiments and by GainSec on the same camera class.
- The image is byte-patched with a small script; header checksum only covers the
  first 512 bytes, so patches outside that range need no checksum fix
  (`tools/build.py` recomputes it anyway).

### Sibling firmware (8 MB GD25VQ64C, different model, useful only as reference)
- Path: `/home/jbaiter/Downloads/Kids GD25VQ64C-SOP8.BIN`
- Same CPU family / same container. Shares ~26% identical 16-byte blocks with
  the KC02, and identical bytes at file 0x400–0x420 (a boot table).
- Its `HANDOVER.md` (`~/Downloads/kids-cam-re/HANDOVER.md`) has a working pi32
  Ghidra recipe from the previous session, but that recipe **does not reproduce
  a clean decode on the KC02** (see below). Its USB analysis is irrelevant here.

---

## 3. The blocker — READ THIS BEFORE WRITING ANY PAYLOAD

**The repo's "function" addresses do not disassemble as functions.**

At `0x0200411C` (claimed `handler_camera` in `ui_state_dispatch`) the bytes are
`00 00 8b bd f2 ff ff 13 00 00 72 a8 86 fe ff 07 …`. Both decoders available
here produce invalid code there and at every other "known" address tested
(`0x02004CC8` "adc_key_scan", `0x02002600`, `0x020093AC`, …):

- Ghidra (pi32 SLEIGH) → `nop; qasl; <Unable to resolve constructor>` (p-code error)
- The repo's own `tools/pi32_disasm.py` → agrees (`lsl; call; nop; ld.x; memor …`)

Meanwhile the bytes at `0x02002600` *do* look like pi32 (`1880`=jhi, `e1 d7`
call/goto pattern, `9c60`=cmp, 32-bit immediates) and are byte-identical to
sequences in the 8 MB sibling — so it is code, just **not where the repo says**.

### What this means
- `ui_state_dispatch` is **not** a table of function pointers (which also
  explains `FIRMWARE_NOTES.md`'s "patch had NO EFFECT" results).
- The repo's address map (handlers, "SD update routine ~0x002F00",
  `adc_key_scan 0x004CC8`) cannot be trusted for code patches.
- **No literal pointers to the string table exist.** Searched the whole image
  for `0x07DE87`/`0x07DE59`/etc. as little-endian, big-endian, and with bases
  0x02000000, 0x07FA0000, 0x07F80000, 0x07FF0000 — zero hits. Strings are
  referenced indirectly.

### Negative results already established (don't repeat these)
- The code region contains **zero** 4-byte values in `[0x0207D000, 0x0207E400]` —
  i.e. the code never loads a pointer into the string table's neighbourhood.
  Strings must be reached via an index table or a computed base, not a literal.
- Iword-byte-swapping the code region does **not** reveal any string pointer.
- No 3-byte / low-24 encodings of `0x0207DE87` exist anywhere.
- The code region does contain 4937 values shaped `0x07FFxxxx` (RAM-like, mostly
  `0x07FFFxxx`) and 111 shaped `0x08xxxxxx`. If strings are copied to RAM at
  boot, look for a second copy of the string data in RAM and a pointer to *that*.

### Hypotheses to test, in order of likelihood
1. **Small-data / base-register addressing.** The compiler may load a base
   (e.g. 0x0207DE00) into a register once and reference strings as
   `base + small_offset`. This would explain the absence of absolute pointers.
   *Test:* look for a 32-bit immediate near the string segment being loaded into
   a register, followed by `lw`/`lbz` with small positive offsets.
2. **Wrong function starts.** `ui_state_dispatch` may be a table of something
   else; the real handlers start nearby but not at these addresses.
   *Test:* linear-scan the app region for pi32 prologues and see where real
   functions begin. The repo's own `pi32_disasm.py prologues` found 130
   "push {rets}" candidates (e.g. 0x0200233A, 0x0200536C, 0x02007FA4, …) but
   its decoder is unreliable.
3. **32-bit instruction p-code gap.** The SLEIGH fails to p-code a class of
   32-bit instructions ("Unable to resolve constructor"). This is a
   `ghidra-jieli` spec limitation, not necessarily a firmware problem.
   *Test:* recompile the `.sla` (done, no change) or port a newer SLEIGH.
   The installed spec was locally patched (`pi32.cspec`: `r15`→`sp`, dropped
   `join` pentry) — try the pristine kagaimiq `ghidra-jieli`.
4. **Code region is scrambled.** Tested `crc` (CrcDecode, key 0xFFFFFFFF),
   `enc` (ENC, keys 0/0xFFFF), `rxgp` over the code region and searched for
   string pointers — no hits. Weak evidence against, but not conclusive.
   *Test:* check the SoC's SFC configuration for an on-the-fly decrypt range
   (the boot table at 0x400 may describe it), or try `jl_sfc_cipher`.
5. **ISA is pi32v2.** Imported with `pi32v2:LE:32:default` — same p-code
   failure at `0x02004120`. Inconclusive; the pi32v2 SLEIGH also has gaps.
6. **Byte-swapped code region.** Untested. Try swapping every 16-bit iword
   before disassembly.

---

## 4. Tooling set up this session (all persisted)

Directory: `~/.src/kc02-fw-mod/ghidra_kc02/`

| File | Purpose |
|---|---|
| `FixPerms.java` | marks the raw-binary block read/write/execute (mandatory) |
| `SeedFuncs.java` | creates functions at every address in `kc02_seeds.txt` |
| `DumpFuncs.java` | exports function map (addr/size/name) to a file |
| `DumpR.java` | linear disassembly of an address range |
| `Decomp.java` | create function + decompile given addresses |
| `ListFuncs.java` | list/decompile functions in an address range |
| `kc02_seeds.txt` | 1457 candidate code addresses (every 4-byte in-code pointer + table entries) |
| `kc02_functions.txt` | 1720 seeded functions (addr, size, name) — **boundaries unverified** |
| `ghidra_project/` | saved Ghidra project `KC02` with the seeded program |
| `ghidra_project_cleanimport/` | pristine import (no seeds) |

Ghidra: `/home/jbaiter/.ghidra/ghidra_12.1.4_PUBLIC`
JieLi processor: `Ghidra/Processors/JieLi/data/languages/` (`pi32:LE:32:default`, `pi32v2:LE:32:default`)

### Reproduce import + seed + export
```bash
GH=/home/jbaiter/.ghidra/ghidra_12.1.4_PUBLIC/support/analyzeHeadless
SCR=~/.src/kc02-fw-mod/ghidra_kc02
DUMP=/home/jbaiter/Downloads/original_firmware_backup.bin

# 1) import with the right language/base and seed functions before analysis
$GH /tmp/kc02full KC02 -import "$DUMP" \
    -processor "pi32:LE:32:default" \
    -loader BinaryLoader -loader-baseAddr 0x02000000 \
    -scriptPath "$SCR" -preScript FixPerms.java -preScript SeedFuncs.java

# 2) export the map
$GH /tmp/kc02full KC02 -process original_firmware_backup.bin -noanalysis \
    -scriptPath "$SCR" -postScript DumpFuncs.java /tmp/kc02_functions.txt

# 3) disassemble / decompile a range or address
$GH /tmp/kc02full KC02 -process original_firmware_backup.bin -noanalysis \
    -scriptPath "$SCR" -postScript DumpR.java 02002600 02002700
```

> NOTE: pre-scripts run before auto-analysis; `FixPerms.java` **must** run in the
> import pass or `disassemble()` silently produces nothing. `SeedFuncs.java` was
> renamed from `Seed.java` to dodge a Ghidra script-compile cache that sticks
> after a failed compile — if you edit a script and it says "class could not be
> found", rename it.

---

## 5. Recommended next steps

1. **Validate one real function.** Take a 32-bit immediate in the code region,
   follow it to a call, and confirm Ghidra decodes both ends. If no address
   produces sane code, jump to hypothesis 1/3/4.
2. **Resolve string references.** Find the code path that consumes `PHOTO/`
   /`DestBin.bin`. Whatever references those strings is necessarily the
   file/update layer and is our anchor. Consider a base-register search (hyp. 1).
3. **Find the FatFs API.** Once any string xref resolves, walk to
   `f_open`/`f_read`/`f_write`/`f_close` (strings `fs : fat16/32,fat ss = %d`
   at 0x0995B9/0x0995E9 are printf in the mount path).
4. **Design the hook** (see below) and only then assemble the payload.
5. **Canary flash first:** take the dump, change `329X_V1.0.0`@0x07DF9F to a
   distinctive string of the same length, flash via SD, confirm the camera
   boots and Settings→Version shows it. Only after that send code.

### Hook design constraints (agreed with operator)
- Payload at XIP `0x02307200` (free region).
- Prefer a **late** init or a **single sacrificial menu action** over a
  boot-vector hook, so a bad payload cannot prevent boot.
- Payload must be bounded: fixed 4 MB loop, fixed chunk, no malloc/scan.
- Include a trigger check (only dump when a marker file exists) so the patched
  firmware behaves 100% stock otherwise.

### Payload sketch (once f_open/f_write are known)
```
f_open(&fp, "DUMP.BIN", FA_CREATE_ALWAYS|FA_WRITE);
for (off = 0; off < 0x400000; off += 0x10000)
    f_write(&fp, (void*)(0x02000000 + off), 0x10000);
f_close(&fp);
return;   // fall through to original behaviour
```

---

## 6. Safety / recovery
- The **only** reliable recovery is desoldering the 4 MB SOP8 (Zetta 25VQ32)
  and reflashing `original_firmware_backup.bin`. Keep that file and its md5 safe.
- Verify the dump before any flash: header checksum 0, 0x307200+ all 0xFF,
  md5 matches.
- Do not trust any code address that has not been confirmed by *two* independent
  methods (Ghidra listing + entry/pointer provenance).
