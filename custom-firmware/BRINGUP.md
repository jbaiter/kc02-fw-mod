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

## Stop model: checkpoints only

`bringup_checkpoint` is a cooperative call boundary, not a trap handler. It
saves the caller's registers, the stack pointer, the link register and `SR`,
switches to the reserved monitor stack, and enters the service loop. GDB can
read registers and memory and continue from the same point.

This means:

- **No arbitrary breakpoints and no single-step.** The monitor does not
  advertise them; the protocol engine returns an empty response for `Z`/`z`
  and refuses `s`.
- **No exception entry handling.** AX3292 HP shadow-bank semantics, `EPCR`,
  `ESR` and PIC state during a real interrupt are not exercised. A trap-based
  implementation still has to be written and validated on silicon.
- **No cache maintenance.** Writes are restricted to a scratch window so no
  instruction-cache invalidation is required to resume.

Register writes are limited to `r2`–`r8`, `r10`–`r31`. The stack pointer, link
register, program counter and `SR` are immutable in this milestone. Memory
reads are limited to application RAM; writes are limited to a 4 KiB scratch
window at `027b0000`. The monitor will not read or write its own buffers, DMA
buffers, NOR, or arbitrary SPRs.

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

# USB build. Supply your own development identifiers; stock camera VID/PIDs,
# and this project's reserved pairs, are refused.
python3 custom-firmware/tools/build_bringup.py \
  --usb-vid 0x1209 --usb-pid 0x0001 --output custom-firmware/build/bringup-usb
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

## Known gaps

- `destbin_inspect` checks the BLDR magic, additive checksum, signature,
  region fields, load address, SFAT marker and full-image CRC, then compares a
  separately snapshotted running prefix against the candidate. **CRC is a
  fingerprint, not authentication.**
- No SD reader and no NOR writer are wired in. The recovery gate reports
  `RECOVERY_NO_SD_BACKEND` in native observation word 5 instead of proceeding.
- The USB backend assumes `EP0` plus a bidirectional `EP1`, per the vendor SDK.
  Independent endpoint operation is unverified on silicon.
- The control path has not been exercised against a real host controller.
  Timing, NAK handling, stall recovery and disconnect behaviour are the likely
  first hardware surprises.
