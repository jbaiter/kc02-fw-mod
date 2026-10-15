# Luma architecture and release contracts

This document describes the implemented core and the contracts for hardware
services that remain unimplemented. It is not a claim that a custom image can
boot, debug, print, enumerate USB, or update an actual camera yet.

## Application and boards

`src/ui.c` owns the three-screen state machine and software renderer. It uses
RGB565 at 320×240 with a checked-in antialiased font, fixed buffers and no heap.
`src/paint.c` blends 4×4 coverage samples along circle and rounded-rectangle
boundaries. It accepts eighth-pixel ring radii for boot animation. Interior and
exterior pixels take constant time; there is no supersampled framebuffer,
floating-point dependency, or blur pass. Geometry checks precede arithmetic
and clipping. Boot artwork, palette and timing load from SD. Shutter animation runs independently
of the physical input debounce. No image filters or extra application modes are
planned.

Camera and gallery share one print policy. Short OK captures in camera and
prints the selected image in gallery; holding OK changes the policy without
also taking the short action. The boot default disables printing. The core can
produce bounded 384-dot raster rows: ordered dither or 32 energy levels.
**Those levels are not calibrated thermal pulse durations.**

`boards/ax3292.c` implements QEMU bring-up through the SoC's SPR peripherals:
ADC, SDHC commands/DMA, CSI YUV buffers, DE scanout and PE11 selection. It does
not read stock application memory or call stock functions. The current code
polls peripherals and disables PIC interrupts. The QEMU machine skips ROM,
clocks and SDRAM bring-up; therefore these builds cannot establish cold boot
on silicon.

Build three RAM applications from the same core with board-specific input and
panel wiring. A single auto-detecting production binary is not implemented.
The physical boot path must identify or retain the board configuration before
initializing GPIO, sensors, or the printer. A flash ID alone is insufficient:
the original and X8D share it.

The linker limits the image, globals, and stack to the Z-12's 2 MiB RAM. Keep
startup, initialized data, BSS clearing and debugger context explicit. The
compiler must emit little-endian OpenRISC instructions without delay slots.

## Storage ownership

One owner at a time: local application, USB host, or updater. A handoff requires
closed files and quiescent DMA. `sd_handoff()` tests admission and blocks local
FAT access while the host/updater owns the card. Returning ownership invalidates
the mount; callers must re-read media geometry and FATs.

A future USB-storage action must flush local writes, close the gallery/theme
reader, disable capture/printing, then export the LUN. On eject/disconnect,
quiesce USB I/O and remount before restoring the UI. Do not let a PC and the
camera modify one FAT concurrently. A missing card is SCSI NOT READY, not a
fabricated writable disk. Do not format media automatically.

The development FAT16 backend supports short names in the root only. It checks
partition geometry, bounds, and both FAT mirrors. It allocates contiguous free
clusters, writes data first, then allocation mirrors, then the directory entry.
It refuses overwrite/delete/format. An I/O error disables further writes for
the mount. This preserves existing files during tested failures, but an
interrupted operation can orphan clusters or leave inconsistent FAT copies.
FAT repair, FAT32, fragmented allocation, hot removal and streaming file access
are still required before general use.

## USB composite device (mass storage and debug implemented; not enumerated)

Implemented today:

| Function | Interface | Endpoints |
|---|---|---|
| USB control / enumeration | EP0 | 0 |
| Mass storage, SCSI transparent BOT | 0, class 08/06/50 | 1 IN/OUT |
| Local GDB transport | 1, vendor class FF (subclass 0x4c) | 2 IN/OUT |

| WebUSB update/staging | 2, vendor class FF, subclass 0x55 | 3 IN/OUT |

The update function exists, with its own subclass so a browser can find it
without guessing. A BOS with the WebUSB platform capability advertises it, which
required `bcdUSB` to become 0x0210: a BOS cannot exist on a 1.1 device.
`GET_URL` is implemented and answers with an https URL when one is configured.

Nothing is hosted, so `iLandingPage` is 0 and no URL is claimed. A browser client
is still to write, and Windows will need an MS OS descriptor binding WinUSB to
the two vendor interfaces while leaving mass storage with its disk driver.

The WebUSB interface cannot be the mass-storage interface: browsers protect
class 08 and the OS owns it. WebUSB must not expose the debugger's arbitrary
memory writes or execution control. On Windows, bind WinUSB to the vendor
interfaces with appropriate MS OS descriptors, leaving MSC with its disk
driver. BOS/WebUSB capabilities and control requests still have to be written
and tested; copying descriptors alone is not a USB stack.

The SDK documents EP0 plus EP1–EP4, and the stock firmware demonstrates
bidirectional EP1 bulk for mass storage. Two independent bulk endpoints is an
assumption, not evidence, and it is the assumption that has to survive first
contact with a host controller. QEMU does not model the AX3292 USB controller,
so the controller layer is written to the documented register contract and has
never enumerated. Required evidence before release: enumeration, BOT reset
recovery, disconnect and timeout behaviour, and real Linux, Windows and browser
sessions. Reuse controller code only where licensing permits; the SDK here is
reference evidence, not redistributable application code.

### Mass-storage ownership

`src/msc_medium.c` binds the LUN to the SD card under the same ownership rule the
filesystem uses. The LUN reports no medium until the handoff, so a host that
enumerates early sees SCSI NOT READY rather than a blank disk; while the host
owns the card the local filesystem refuses every call; taking ownership back
invalidates the mount. FORMAT UNIT and EJECT are refused, and a truncated WRITE
is a phase error rather than a silent partial write.

## USB GDB (protocol engine and first-stage monitor implemented; no silicon test)

`src/debug.c` implements bounded, acknowledged GDB RSP framing/checksums,
retransmission, target XML, register and memory access, continue, detach and a
Ctrl-C callback. It accepts at most 1023 payload bytes and limits a memory
transaction to 256 bytes. It rejects numeric overflow, malformed hex, out-of-range
register numbers and unauthorised requests. Unsupported operations return an
empty response; absent backends return errors.

The first-stage monitor (`bringup/`, `src/monitor.c`, `src/usb_device.c`,
`src/ax_usb.c`) now supplies the target side for the original KC02, and the same
stage runs a read-only recovery gate (`src/recover.c`) before any application. It runs
before any application UI, preserves the factory boot chain, and validates a
stock-format candidate read-only. What it does **and does not** do is documented
in [BRINGUP.md](BRINGUP.md). In summary:

1. Device-local debug authorization exists: a configured USB device alone
grants nothing. The keypad sequence release-OK/press-OK/release-OK enables the
monitor; a bus reset, disconnect, suspend, reconfiguration, vendor revoke or
protocol fault disables it and leaves the target stopped.
2. Memory access is restricted: reads to application RAM, writes only to a
4 KiB scratch window. Stack pointer, link register, PC and SR are immutable.
3. **Stopping uses real exception vectors.** The stage owns a vector table at
its own `EVBA`, so `l.trap` stops at an arbitrary instruction. `Z0`/`z0`
install and remove software breakpoints, every patch is tracked and lifted on
detach or revoke, and single-step places temporary traps on the decoded
successors of the current instruction. A refused step is an explicit error.
Absent on purpose: cache maintenance verified on silicon (the vendor `ICAIR`
sequence is written but the emulator treats it as inert), AX3292 HP shadow-bank
capture, and a timer interrupt, so Ctrl-C only takes effect at the next trap.
An instruction whose successor cannot be determined is refused, not guessed.
4. The USB controller driver is a single polled bidirectional bulk endpoint
plus EP0 control, matching the vendor SDK's documented register map. QEMU does
not model this controller, so only a stable polled wait is verified in
emulation. Enumeration, transfer timing, NAK and stall recovery, and disconnect
behaviour are untested on hardware.

A real `gdb-multiarch` client is tested through the full host chain: GDB to the
localhost bridge, bridge to the AX3292 register/DMA contract model, model to the
actual monitor and RSP engine. That test proves the protocol and access policy.
It is not a physical USB test.

Wire registers: **r0–r31, ppc, npc, sr**, all 32-bit little-endian. The `g`
response is exactly 280 ASCII hex characters. This matches the target XML;
`sr` does not claim generic OpenRISC EPH semantics. A real `gdb-multiarch` test
uses `set architecture or1k` and `set endian little` against a host stop-monitor
harness. It does not validate exception entry, USB or cache control.

The physical implementation requires:

1. Device-local debug authorization; the normal browser update interface cannot
   grant it. Revoke authorization on detach, reset and unplug.
2. A stop monitor with its own reserved stack/buffers and endpoint service loop.
   It must keep servicing USB while application scheduling is stopped. Avoid
   application allocators, filesystem locks and logging callbacks.
3. Exception assembly that preserves the complete live register context,
   AX3292 HP shadow-bank semantics, EPCR/ESR and PIC state. First validate an
   intentional RAM trap and resume on hardware.
4. Checked readable/writable RAM regions. Deny monitor memory, live USB/DMA
   buffers, arbitrary SPR access and NOR writes through RSP. Program writes
   require data-cache clean and instruction-cache invalidation before resume.
5. Verified software/hardware breakpoints and single-step respecting no-delay
   control flow. Current test callbacks deliberately refuse single-step; the
   protocol engine does not advertise it as working.
6. A PyUSB-to-localhost bridge for a normal GDB client. Claim only the debug
   vendor interface; do not detach the mass-storage driver. Bind TCP to
   127.0.0.1 and close access when the device revokes debug authorization.

Before entering debug, finish pending capture/print work and quiesce/eject an
exported SD LUN. During a stop, return NOT READY for application-dependent MSC
operations rather than block USB on a halted thread. Debugging during an update
or with an energized print head must be rejected.

## Shared update engine (NOR model tested; physical installation pending)

Both WebUSB and `/firmware.img` must use `src/update.c`. They supply a bounded
stream reader and may not choose arbitrary flash addresses. Validation reads
the full payload before mutation, checks format/size/board/version and CRCs,
then programs the inactive application slot. It verifies read-back before
writing a final commit record. A changed source between validation and copying
cannot commit unless the programmed payload matches the expected CRC.

CRC32 detects corruption; **it does not authenticate firmware**. Require explicit
physical confirmation of an update in addition to browser permission. Firmware
signatures / trusted keys are not implemented. Do not present CRC or a version
counter as a secure-boot guarantee.

The engine also requires a positive `recovery_verified` gate. The RAM builds
have no NOR backend and never enable this gate. The following layout is a
**proposal used by the NOR tests**, not the stock camera's layout:

| NOR range | Role |
|---|---|
| `000000–00ffff` | Protected, board-aware boot/recovery loader |
| `010000–010fff`, `011000–011fff` | Independent commit sectors A / B |
| `020000–19ffff` | Application slot A (1.5 MiB) |
| `1a0000–31ffff` | Application slot B (1.5 MiB) |
| `320000–3fffff` | Reserved |

The application updater never writes the recovery area or the active slot.
The boot selector checks both commit CRCs and both payload CRCs and chooses the
newest valid generation. A failed write may leave a dirty inactive slot; the
previous valid slot remains selected. Equal generations are ambiguous and fail
closed. Generation wrap and non-increasing release versions refuse an update.

The tests cut power at every mutation boundary and every byte of the commit
record, including a lost response after a complete write. Reconnect must inspect
the selected version before retrying. **Real interrupted erase/program behavior,
flash protection, supply supervision and recovery are not validated.** A first
installation needs a separate board-aware loader and verified recovery procedure;
this engine refuses to initialize an empty flash. It must never overwrite the
factory loader as a side effect of an application update.

### Proposed `firmware.img` format v1

All integer fields use little-endian. Header length is 64 bytes; the file ends
immediately after the declared payload.

| Offset | Field |
|---|---|
| 0 | `LUMAIMG\0` (8 bytes) |
| 8 | u16 format version = 1 |
| 10 | u16 header bytes = 64 |
| 12 | u32 board mask: original=1, X8D=2, Z-12=4 |
| 16 | u32 payload bytes, 1…`SLOT_SIZE` |
| 20 | u32 load address = `0x02000000` |
| 24 | u32 entry address = `0x02000000` |
| 28 | u32 nonzero release version |
| 32 | u32 payload CRC32 (IEEE) |
| 36 | u32 header CRC32 (IEEE), calculated with this field zero |
| 40 | u32 flags; production validator requires zero |
| 44–63 | Reserved, must be zero |

Flag 1 denotes a development-only image and the updater rejects it. The current
build produces `luma.elf` and `luma.ram.bin`, not installable containers. There is
no production image packer or flash command yet.

Each commit record is 32 bytes: `LMCOMMIT` followed by six u32 values:
generation, slot index, payload length, payload CRC32, release version, and CRC32
of the first 28 bytes. The metadata sector and slot index must agree. The payload
has a fixed RAM load/entry address; no executable header comes from a theme.

The SD adapter now exists in `src/update_sd.c`: it finds `FIRMWARE.IMG` in the
card root, reports version, board mask and size for confirmation without writing
anything, refuses while the host owns the card, validates the whole file before
the engine may mutate a byte, and reports `NO_WRITER` when there is no NOR
backend rather than a refusal or a success. A successful check reports `READY`,
never `OK`, so validation cannot be mistaken for installation.

The WebUSB adapter exists in `src/webusb.c`. It stages a complete bounded
container on the card under its own name, verifying each file's CRC as the bytes
arrive, and only then validates and commits:

- Firmware goes through the same `image_validate`/`update_apply` engine as the SD
  path, and is still refused without a device-local confirmation and a verified
  recovery gate. A browser cannot confirm its own update.
- Themes are validated as a set and committed images-first, config-last, so an
  interruption leaves a theme that still renders.

Staging under `UPDATE.NEW` rather than the final name is deliberate: a
half-received upload must never overwrite a good image or a working theme.

### Legacy `DestBin.bin` candidate inspection

The first installation cannot use the A/B layout: it has to arrive as a
stock-format image the factory updater accepts. `src/destbin.c` is a read-only
inspector for that path, original KC02 only. It checks the BLDR magic, the
512-byte additive checksum, the `55AA` boot signature, the declared load
address, region fields, the `SFAT` marker and the full-image CRC, then compares
a separately snapshotted running prefix against the candidate and refuses any
change inside the boot/recovery region.

It contains no erase or program callback, no executable loading and no writer.
CRC is a fingerprint, **not authentication**, and matching the stock layout is
not proof that a candidate boots. `tools/factory.py` packages a candidate from
the pinned original dump and enforces that only the factory application copy
span changes.

`src/recover.c` is the gate that runs first with the card. It mounts the volume
read-only through the shared SD driver, streams a root `DESTBIN.BIN` through one
staging sector, validates it and reports a distinct code for each failure. It
stops at `RECOVER_PREFIX_UNAVAILABLE` rather than claiming success, because
`destbin_prefix_matches` has no trusted snapshot of the running boot bytes yet.
See [BRINGUP.md](BRINGUP.md) for the codes and the evidence.

## Next hardware milestones

1. RAM-only physical bring-up of the new clock/timebase, display, keypad and
   SD driver; add proper sensor initialization. Do not infer these from QEMU
   register read-back. The first-stage stage already reuses the factory clock,
   SDRAM and pin configuration instead of redoing bring-up from reset.
2. A trap-based stop monitor: exception entry that preserves AX3292 HP
   shadow-bank semantics, EPCR, ESR and PIC state, plus verified breakpoints
   and no-delay-aware stepping. The current cooperative checkpoint cannot
   stop at arbitrary code.
3. USB enumeration and MSC on real hardware; preserve a known recovery route
   throughout. The debug endpoint transport is written but never enumerated.
4. Printer interlocks and energy calibration per board before enabling strobes.
5. A working SD recovery reader and NOR writer for `DestBin`-format first
   installation, then the A/B `firmware.img` path with interrupted-update
   validation on sacrificial hardware.

USB debugging, mass storage, both update paths and three-board hardware support
are required release criteria. The RAM milestone does not complete them.
