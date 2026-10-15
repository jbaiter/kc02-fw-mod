# First-stage bring-up: recovery gate and USB debugger

This stage sits between the factory boot chain and any application UI. It is
experimental and **not flash-qualified**. No physical KC02 has run it.

## What it is for

The stage exists so that the first custom image on a camera is the most
recoverable thing we can ship, and so we can inspect that image while it runs.
It does three things:

1. Takes over the application handoff at the stock entry address.
2. Serves a bounded GDB Remote Serial Protocol monitor over USB vendor bulk.
3. Validates a stock-format `DestBin.bin` candidate before any writer sees it.

It deliberately does **not** draw a UI, touch the sensor, mount SD, or write
NOR. Those are later milestones. Until they exist, the probe flags report a
blocked state rather than pretending an absent driver means "no update file".

## Boot handoff

The factory boot chain copies the application image to `0x02000000` and jumps
to the entry recorded in the BLDR header. For the original KC02 that is
`0x0200046c`. The stage keeps the factory header, boot code, SDRAM initializer
and resources, and replaces only the bytes the boot chain actually copies to
SDRAM:

| Region | Contents |
|---|---|
| `000000`–`0025ff` | Factory boot/header. Never modified. |
| `002600`–`0cae00` | Replaced by the candidate image (factory copy span). |
| `0cae00`–`3fffff` | Factory resources. Never modified. |

`tools/factory.py` enforces that span and refuses to package anything else. It
also refuses the install filenames (`DestBin.bin`, `DestBin.img`,
`firmware.img`) so a review artifact cannot be dropped onto a card by accident.

Two properties of this handoff matter:

- **Clock, SDRAM and pin configuration come from the factory boot chain.** This
  is not a ROM loader and does not bring silicon up from reset.
- **Only the original KC02 dump is an admissible template**, matched by pinned
  SHA-256. The X8D and Z-12 need their own template work.

The retained boot code is still erased and rewritten by the stock updater
during installation, so retaining it does not make a first installation
atomic. Real power-loss behaviour is unverified.

## Layout in RAM

| Address | Contents |
|---|---|
| `02000000` | Bootstrap: entry stub plus relocation |
| `02002000` | Workload: the call site that enters the monitor |
| `02002800` | Native observation words, read-only from the host |
| `027c0000` | Resident monitor, relocated from its load image at `02003000` |
| `027b0000` | Application stack |
| `027ffff0` | Reserved monitor stack |

The bootstrap relocates the monitor and clears BSS before running C. If the
data cache is enabled at handoff, it stops in a self-loop rather than writing
back dirty application data into a region it is about to reuse. It does not
discard live cache contents.

## The recovery gate

The gate runs first, before the workload and before any USB setup, and it is
read-only. Card bring-up uses the shared SD driver in `src/sd_hw.c`, the same
one the Luma application uses, so there is a single implementation of the
vendor command/DMA contract. The volume is mounted with a null write callback,
which makes `fat_create` refuse. The gate cannot publish a file.

The candidate is streamed, not buffered. `fat_stream_read` walks the cluster
chain through one 512-byte staging sector and one cached FAT sector, so a 4 MiB
image costs no 4 MiB allocation. The validator reads strictly in ascending
order, which is the only access pattern a forward-only stream can honour.

Every outcome is distinct and terminal:

| Code | Meaning |
|---|---|
| 0 | `RECOVER_OK`: reserved; the gate never reports a verified candidate today |
| 1 | No card, or it did not answer as SDHC |
| 2 | Card present, no usable FAT16 volume |
| 3 | No `DESTBIN.BIN` in the root directory |
| 4 | The file could not be read completely |
| 5 | Structure, size or layout rejected |
| 6 | Structure valid; the boot-prefix check could not run |

Code 6 is the one that matters. `destbin_inspect` checks the BLDR magic, the
512-byte additive checksum, the `55AA` signature, the declared load address,
the region fields, the `SFAT` marker and the full-image CRC, and reports that
CRC as a **fingerprint**. `DestBin` stores no image CRC of its own, so a bit
flip in the resource area is structurally valid and no amount of structure
checking will catch it. Comparing the fingerprint against a known-good build is
the only defence for those bytes. It is not authentication.

The remaining check is `destbin_prefix_matches`, which compares the first
`protected_bytes` of the candidate against a trusted snapshot of the running
boot/recovery bytes. No such snapshot exists yet, so the gate reports code 6
instead of claiming verification. Reporting OK without that comparison would be
the most dangerous thing this stage could do.

## Stop model: traps, breakpoints and stepping

The stage installs its own exception vector table and points `EVBA` at it. The
table follows the vendor layout in `reset.S`: sixteen 16-byte slots, one `l.j`
each, indexed by `exception << 4`, so slot 0x0e0 is `l.trap` and 0x0c0 is
`l.sys`. Each slot jumps to a stub that records the exception number, and the
shared entry saves all 32 registers plus `EPCR` and `ESR`, switches to the
reserved monitor stack, and hands C a frame.

There are two ways in:

| Entry | Used by | Notes |
|---|---|---|
| Cooperative call boundary (`checkpoint.S`) | The workload's own call sites | Cheap, but only stops where the target asks |
| Real exception entry (`vectors.S`) | `l.trap`, faults, any trap word the monitor installs | Stops at an arbitrary instruction |

### Breakpoints

`Z0,<address>,4` writes an `l.trap` word over one instruction and remembers the
original; `z0` puts it back. Every patch is tracked, and detach, unplug and bus
reset lift all of them, so the target is never left with a trap where real code
belongs.

A breakpoint may only be placed inside the code region the stage declares, and
never over an existing trap word. Eight at a time.

### Single stepping

`l.rfe` has no successor worth guessing, and a real debug monitor does not get to
guess, so `or1k_decode.c` decodes control flow and places temporary traps on the
**successors** of the current instruction:

| Instruction | Temporary traps |
|---|---|
| `l.j`, `l.jal` | The target |
| `l.bf` | The target and the fall-through |
| `l.bnf` | The fall-through and the target |
| `l.jr`, `l.jalr` | The destination register's value |
| `l.rfe` | Refused |
| `l.trap`, `l.sys` | Refused: the instruction stops |
| Anything else | `pc + 4` |

The decoder is small on purpose and its opcodes come from QEMU's own
`target/or1k/insns.decode`. Three things keep it honest: host tests compare its
successors against instructions the toolchain actually assembled, the in-guest
self-test single-steps a sequence whose conditional branch is **taken** so a
decoder that only traps `pc + 4` lands on the wrong path, and a branch back to
its own address is refused rather than turned into a step that silently never
returns.

`continue` from a breakpoint lifts that breakpoint, steps over it, and swallows
the intermediate stop, so the debugger only sees the next real event. The
breakpoint is re-armed as part of the same step.

### Access policy

Reads cover the declared RAM region, writes only the 4 KiB scratch window.
`SP`, the link register and `SR` are immutable; `npc` is writable for a trap stop
only, and only into the code region. A refused step is an explicit error rather
than silence, so a client can say why.

### What this is not

- **No cache maintenance on silicon.** Writing a trap word needs the target's
  fetch to see it, which is `code_sync` in `bringup/main.c`: the vendor's
  invalidate sequence from `ax32xx_sysIcacheInit`, writing one line address to
  `SPR_ICAIR`. The emulator backs `ICAIR` as an inert register, so this path
  follows the vendor contract and is **not verified sufficient**. A wrong or
  missing flush shows up as a trap that never fires.
- **No HP shadow-bank capture.** An HP interrupt entry shadows registers in
  hardware, and `HELPER(rfe)` restores them in the model. The monitor reports
  the exception number and the pre-exception `SR`, but it does not yet validate
  the shadow-bank round trip on silicon.
- **Ctrl-C cannot halt a free-running target.** The interrupt request is
  recorded and takes effect at the next trap or checkpoint. Doing better needs a
  timer interrupt, which this stage does not enable.
- **`EVBA` selection is unverified on hardware.** The SDK's SR bit 14 is named
  `intvct_sram/intvct_mask` and documented as selecting SRAM versus mask-ROM
  vectors. This image leaves it clear and points `EVBA` at its own table. The
  emulator dispatches to `EVBA` regardless, so the SRAM-vector variant has to be
  confirmed on a real unit.

## USB mass storage

The device is a composite of two functions, because a browser may claim a
vendor interface but can never be given class 08:

| Function | Interface | Endpoints |
|---|---|---|
| Mass storage, class 08/06/50 | 0 | EP1 IN/OUT, 64-byte bulk |
| Vendor debug, class ff/4c/01 | 1 | EP2 IN/OUT, 64-byte bulk |

`src/msc.c` implements Bulk-Only Transport with a SCSI transparent command set:
INQUIRY, REQUEST SENSE, TEST UNIT READY, READ CAPACITY(10), READ(10), WRITE(10),
SYNCHRONIZE CACHE, MODE SENSE(6)/(10), REPORT LUNS, PREVENT ALLOW MEDIUM
REMOVAL, START STOP UNIT.

**The card is handed over, never shared.** `src/msc_medium.c` reports no medium
at all until `sd_handoff` gives the card to the host, so a host that enumerates
before the handoff sees SCSI NOT READY rather than a blank disk. While the host
owns the card every local filesystem call is refused, and taking it back
invalidates the mount so the camera must remount. A bus reset or disconnect
reclaims the card; configuring the device hands it over.

Three refusals matter as much as the commands:

- **FORMAT UNIT is refused.** This device never formats, erases or reinitialises
  media.
- **EJECT is refused**, so a host cannot eject a card the camera owns.
- **A truncated WRITE is a phase error**, not a silent partial write: the host
  believing it wrote blocks that never landed is worse than any error code.

A medium error sends a failed CSW with the residue so the host can ask REQUEST
SENSE. Stalling the pipe the CSW travels on would hide a recoverable error
behind what looks like a dead endpoint.

### What is modelled and what is not

`src/ax_usb.c` drives two bulk endpoints through their documented registers: one
address register pair each, `USB20CON1` bits 13+epx for the transfer length, and
per-endpoint interrupt bits for collection and completion. Endpoint selection
uses those interrupt bits rather than trying each endpoint in turn, because a
controller has one RXCSR visible per index and guessing would let the storage
endpoint take the debug endpoint's packet.

QEMU does not model this controller, so **none of it has enumerated**. Timing,
NAK behaviour, and whether two independent bulk endpoints work at all remain
unverified: the vendor SDK documents EP0 plus EP1..EP4, but only EP0 and EP1 are
demonstrated by the stock firmware.

## The card update path

`src/update_sd.c` finds `FIRMWARE.IMG` in the card root, validates it, and hands
it to the shared update engine. Discovery and validation are separate from
installation, so a UI can offer a version for confirmation without touching
anything:

| Result | Meaning |
|---|---|
| `READY` | Found and valid, and nothing has been written |
| `OK` | Committed |
| `NO_CARD` | No volume mounted |
| `BUSY` | The host owns the card; not ours to read |
| `NO_FILE` | No `FIRMWARE.IMG` in the root |
| `BAD_FILE` | Size, header, board or CRC rejected |
| `NO_WRITER` | Validated, and there is no NOR backend |
| `DENIED` | No physical confirmation, or no verified recovery gate |
| `STALE` | Older than what is already installed |
| `NO_BOOT` | Neither slot holds a bootable record |
| `IO` | A read failed, or an erase, program or verify did |

`READY` and `OK` are deliberately different words. A check that reported `OK`
could be read as "installed", which is the one thing it must never imply.

The reader is a bounded streaming source: a 1.5 MiB image costs no 1.5 MiB
buffer. The engine reads its source in two ascending passes, so a rewind reopens
the file rather than pretending to seek, and a forward skip reads and discards in
fixed steps.

Three refusals carry the weight:

- **`BUSY` while the host owns the card.** Two writers on one FAT is precisely
  what the ownership rule exists to prevent, so the update does not even read
  the file.
- **`NO_WRITER` rather than a refusal or a success.** The image is good and this
  build cannot install it. Saying so is more useful than either extreme.
- **`DENIED` without physical confirmation and a verified recovery gate.** This
  adapter cannot supply either, so it cannot install on its own.

## WebUSB: firmware and theme updates from a browser

Three functions share one device, and a browser can claim exactly one of them:

| Function | Interface | Class | Endpoints |
|---|---|---|---|
| Mass storage | 0 | 08/06/50 | EP1 IN/OUT |
| Vendor debug | 1 | ff/4c/01 | EP2 IN/OUT |
| WebUSB update | 2 | ff/55/01 | EP3 IN/OUT |

The update function has its own subclass, so the browser client can find it
without guessing, and the debugger's memory access is not reachable from it.

Descriptors follow the specification and Chrome's implementation:

- **BOS** (type 0x0F) with one platform capability (type 0x10, capability 0x05).
  The WebUSB UUID is written in the little-endian byte order Chrome compares
  against; reversed, the capability is invisible with no error anywhere.
- `bcdUSB` had to become 0x0210. A BOS cannot exist on a 1.1 device, so without
  that change the capability would never be seen.
- **`GET_URL`**: `bmRequestType` 0xC0, `bRequest` = the advertised vendor code,
  `wValue` = iLandingPage, `wIndex` = 2. The value and index are the opposite way
  round from what they look like they should be. Only `https` is offered: a
  plaintext landing page would let anyone on the path change the page that talks
  to the device.
- **No landing page is hosted, so none is claimed.** `iLandingPage` is 0 and
  `GET_URL` stalls. The mechanism is implemented and tested; only the page is
  missing, and a URL that does not exist would be worse than none.

### The upload

A browser sends one container on the update endpoint and drives it with three
control requests: `BEGIN`, `COMMIT`, `ABORT`, plus `STATUS` to poll.

```
header (24)  "LUMAUPD\0", u16 format, u16 header_bytes, u16 kind (1 firmware,
             2 theme), u16 count, u32 total_bytes, u32 payload crc32
entry (20)   name[11] space-padded 8.3, pad, u32 length, u32 crc32
payload      the file bytes, concatenated in entry order
```

The entry is 20 bytes and not 16. An 11-byte name plus a pad plus two u32 fields
does not fit in 16, and a 16-byte entry puts the CRC over the last three bytes of
the name, corrupting every extension.

The container is written to the card as one staging file, `UPDATE.NEW`, while its
CRCs are verified as the bytes arrive. Only when it is fully written and checked
does `COMMIT` do anything:

- **Firmware**: the staged artifact is validated with the same engine the SD path
  uses and applied through `update_apply`. Applying still needs a device-local
  confirmation that USB cannot supply, and there is no NOR driver, so a valid
  image is reported as such and refused.
- **Theme**: the bundle is validated as a set and then committed, images first
  and the config last.

Staging under its own name matters: a half-received upload must never overwrite a
good `FIRMWARE.IMG` or a working theme. A failed CRC aborts the staging write, and
an abort frees the new allocation without touching what was there.

## Debug authorization

USB cannot grant debug. The USB device implements no vendor request that
authorizes the monitor; `SET_ADDRESS`, `SET_CONFIGURATION` and the standard
endpoint requests are the whole control surface.

Authorization happens locally, on the camera's own keypad: after the device is
configured, release OK, press OK, then release OK. Only then does the monitor
answer `g`, `m` and `c`. Authorization is revoked by a bus reset, a disconnect,
a suspend, `SET_CONFIGURATION`, a vendor revoke request, or a protocol fault.
Revocation leaves the target **stopped**; it never silently resumes an
inspected application.

`tools/usb_gdb.py` is a localhost bridge for a normal `gdb-multiarch` client.
It binds to `127.0.0.1`, refuses to detach a kernel driver, refuses to claim an
unexpected descriptor set, and treats a partial USB write as a failure rather
than retrying it. `set configuration` is never issued, because that would reset
and revoke an established session.

## Building and running

```sh
# Original KC02 candidate. Defaults to USB detached and no self-test.
python3 custom-firmware/tools/build_bringup.py

# QEMU checkpoint/relocation self-test (USB never attached).
python3 custom-firmware/tools/build_bringup.py --selftest \
  --output custom-firmware/build/bringup-selftest
python3 custom-firmware/tools/bringup_smoke.py \
  --build custom-firmware/build/bringup-selftest \
  --output ~/.cache/kc02-emulator/luma-bringup-NEW

# Recovery gate against a private card holding a candidate DESTBIN.BIN.
python3 custom-firmware/tools/bringup_smoke.py \
  --build custom-firmware/build/bringup-original \
  --card /path/to/private-card.img \
  --output ~/.cache/kc02-emulator/luma-bringup-recovery-NEW

# USB build. Supply your own development identifiers; stock camera VID/PIDs
# are refused.
python3 custom-firmware/tools/build_bringup.py \
  --usb-vid 0x1209 --usb-pid 0x0001 --output custom-firmware/build/bringup-usb
python3 custom-firmware/tools/bringup_smoke.py --usb-wait \
  --build custom-firmware/build/bringup-usb \
  --output ~/.cache/kc02-emulator/luma-bringup-usbwait-NEW
```

There is no production device identity yet. Compiled defaults leave USB
detached, so an untested controller path cannot claim a stock camera's USB
identity on a bus.

QEMU does not model the AX3292 USB controller. The USB build is therefore only
verified to reach a stable polled wait without crashing; `.cache/kc02-emulator/
luma-bringup-usbwait-01` records that run. **Enumerating, transferring and
servicing GDB over real USB is untested.**

## Candidate packaging

```sh
python3 custom-firmware/tools/factory.py \
  --stock firmware/original.bin \
  --ram custom-firmware/build/bringup-original/bringup.ram.bin \
  --manifest custom-firmware/build/bringup-original/manifest.json \
  --output /some/private/path/DestBin.candidate.bin
```

The output file and its `.json` report are created exclusively; the tool
refuses to overwrite an existing candidate or report, and refuses a
`--ram` manifest that is not the original-board bring-up artifact. It never
writes to a camera or a card.

The report sets `flash_qualified`, `recovery_writer` and `hardware_tested` to
false. They stay false until an external SPI read has been verified on the
target unit and the stock updater has been proven with an unchanged image.

## Evidence

| Artifact | What it shows |
|---|---|
| `~/.cache/kc02-emulator/bu-trap-final` | Breakpoint installed, hit, stepped over, removed and restored; PC sequence matches the assembly in `bringup/selftest.S`, including the taken branch at `+0x10` and the indirect return |
| `~/.cache/kc02-emulator/bu-usbwait-final` | USB build reaches and holds the polled wait, gate reports code 1 with no card |
| `~/.cache/kc02-emulator/msc-selftest2` | Relocation, checkpoint cycle and the trap/step sequence, `failures: 0x0` |
| `~/.cache/kc02-emulator/msc-update3` | A card carrying both `DESTBIN.BIN` and `FIRMWARE.IMG` driven through the guest's own SD driver and FAT reader: recovery code 6, and the update path reporting `READY`, version 12 and 10000 validated payload bytes |
| `~/.cache/kc02-emulator/msc-card` | Gate finds a candidate on a FAT16 card and reports code 6 with fingerprint `0x95eceee0`, matching the host's `zlib.crc32` |

The self-test drives the real RSP engine, the real vectors, the real step logic
and the real monitor, and checks the exact bytes the monitor puts on the wire.
Only the transport is stood in for, because the emulator does not model the USB
controller. Every expectation has its own failure bit, so a failure names itself,
and the run records `failures: 0x0`.

The card update path is covered by `tests/test_update_sd.py`, which builds a real
FAT16 card on disk, streams `FIRMWARE.IMG` out of it, and drives the engine into
a model NOR: version and board reported for confirmation, a lowercase filename
treated as the same file, nine malformed or mismatched images refused before
anything is written, a corrupt payload caught by CRC, a card owned by the host
refused, `NO_WRITER` for a validated file with no backend, the confirmation and
recovery gates enforced, a real two-pass update committing the inactive slot
with the active slot untouched, a stale image refused, flash with no bootable
slot refused, a truncated read and a refused program both committing nothing, and
the card still readable afterwards.

WebUSB is covered by `tests/test_webusb.py`, where a peer plays the browser: it
reads the BOS and the landing page, sends a container on the update endpoint and
reads the status back. Both update kinds are exercised over a real FAT16 card,
including that nothing is written before `COMMIT`, that a firmware update reaches
the inactive NOR slot byte for byte, that a browser cannot confirm its own
update, that a mangled transfer and eight malformed containers are refused, that
a theme with an unreferenced file is refused and the file is not created, and
that an abort leaves the card exactly as it was.

Mass storage is covered on the host by `tests/test_msc.py`, where a peer acts as
the USB host: it builds real CBWs, drains the bulk-IN pipe and parses the CSW,
so the tests are protocol conversations rather than calls into the engine's own
helpers. `tests/test_bringup.py` then drives a full BOT session through the
composite descriptors, the control layer and the real endpoints, including the
ownership gate, `GET MAX LUN`, Bulk-Only reset and unit attention.

`tests/test_bringup.py` covers the rest of the logic on the host in milliseconds:
breakpoint install/hit/step/re-arm/remove, step refusal for `l.rfe`, `l.trap`,
`l.sys` and self-branches, both successors of a conditional branch, continue
from a breakpoint, detach lifting every patch, the `npc` write policy, decoder
successors pinned to real assembled encodings, and the recovery gate.

All runs record `usb_tested: false`, `physical_hardware_tested: false`,
`flash_written: false`.

## Two byte orders, and why mixing them looks like a broken target

`m`/`M` carry memory bytes in address order, so a little-endian word appears
byte-reversed on the wire. `p`/`P` carry register values in the target's byte
order, which is also little-endian here. Both are consistent and GDB-compatible;
comparing a register read against the numeric hex of its value is a reliable way
to conclude the target is broken when it is not. That mistake cost real time
during this milestone, which is why `le_hex` and `mem_hex` exist in the tests.

## Known gaps

- No NOR writer is wired in, and the boot-prefix comparison has no trusted
  snapshot, so the gate stops at code 6. **CRC is a fingerprint, not
  authentication.**
- **Card capacity units are a vendor quirk.** `sd_hw_init` reproduces
  `hal_sd.c`'s `((csize+1)<<10)`, which is numerically a sector count rather
  than a byte count; the vendor shifts by 10 where SDHC means 19. A real byte
  count would overflow `u32` above 4 GB, so the value is kept and compared
  against sector offsets. `sd_hw.h` says so instead of calling it bytes.
- The USB backend assumes `EP0` plus two bidirectional bulk endpoints, per the
  vendor SDK. Independent endpoint operation is unverified on silicon, and the
  control path has never met a real host controller. Timing, NAK handling, stall
  recovery, and disconnect behaviour are the likely first hardware surprises.
- A misbehaving host can still fill the storage endpoint's staging buffer. That
  faults the pipe rather than dropping received bytes, because dropping them
  would corrupt a write, but it is a fail-closed guess, not a validated
  recovery path.
- The LUN's `sync` callback is absent, so SYNCHRONIZE CACHE reports success
  without proving anything reached the card.
- The update path has no NOR backend, so it can validate and refuse but never
  install. Everything downstream of `update_sd_apply`'s writer check is untested
  on real flash, and the engine's tests use a model.
- **No browser has ever talked to this device.** Chrome's own parsing of the BOS
  and its handling of `GET_URL` are unverified; what is verified is that the
  bytes match the specification and that the staging protocol works end to end
  against a peer. No landing page is hosted, so the browser cannot offer one.
- The update interface's third endpoint pair is the same assumption as the second:
  the vendor SDK documents EP0 plus EP1..EP4, and the stock firmware demonstrates
  only EP0 and EP1.
- The control reply is now streamed in endpoint-sized packets, because the
  composite configuration descriptor is 78 bytes and does not fit one packet.
  That path is exercised in the harness, not on silicon.
- The stage owns the card for the length of the gate. Nothing else has started
  yet, so that is safe today, but a future application launch must not run
  concurrently with an unremounted card.

## Where the boot-prefix snapshot has to come from

Until a NOR read path exists, the gate cannot finish its last check. The
options, best first:

1. A verified read of the mapped boot region, with its own bounds and no
   dependence on the application being intact.
2. A build-time constant recorded from the pinned original dump, which proves
   nothing about the unit actually running.
3. Installing without the comparison, which this project rejects.

Option 1 is the next real milestone for the recovery path.
