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
| `~/.cache/kc02-emulator/luma-bringup-selftest-final` | Monitor relocated, 91k+ checkpoint stop/resume cycles, gate reports code 1 with no card |
| `~/.cache/kc02-emulator/luma-bringup-recovery-final` | Gate finds a candidate on a FAT16 card and reports code 6 with fingerprint `0x95eceee0`, matching the host's `zlib.crc32` |
| `~/.cache/kc02-emulator/luma-bringup-usbwait-final` | USB build reaches and holds the polled wait |

All three record `usb_tested: false`, `physical_hardware_tested: false`,
`flash_written: false`. Host tests live in `tests/test_bringup.py`: they compile
the real sources and drive an actual `gdb-multiarch` through the localhost
bridge, the AX3292 register model and the actual monitor.

## Known gaps

- No NOR writer is wired in, and the boot-prefix comparison has no trusted
  snapshot, so the gate stops at code 6. **CRC is a fingerprint, not
  authentication.**
- **Card capacity units are a vendor quirk.** `sd_hw_init` reproduces
  `hal_sd.c`'s `((csize+1)<<10)`, which is numerically a sector count rather
  than a byte count; the vendor shifts by 10 where SDHC means 19. A real byte
  count would overflow `u32` above 4 GB, so the value is kept and compared
  against sector offsets. `sd_hw.h` says so instead of calling it bytes.
- The USB backend assumes `EP0` plus a bidirectional `EP1`, per the vendor SDK.
  Independent endpoint operation is unverified on silicon, and the control path
  has never met a real host controller. Timing, NAK handling, stall recovery and
  disconnect behaviour are the likely first hardware surprises.
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
