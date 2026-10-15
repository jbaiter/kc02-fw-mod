# Handover: Luma and the hardware bring-up

This is the authoritative handover for the **Luma / bring-up / USB / update**
workstream, and for the **hardware session** that is about to start.

Read this before [`HANDOVER_EMULATOR.md`](HANDOVER_EMULATOR.md). That document
covers the earlier workstream (running the *unchanged stock* firmware in QEMU) and
its "CURRENT STATE" sections name a commit from months ago. Its emulator facts are
still good; its status is not. For the current state, this file wins.

- Repo: `/home/jbaiter/.src/kc02-fw-mod`, branch `reversing`, HEAD `1c3ec9d`.
- Working tree clean. **362 tests pass, 5 skipped.**
- Nothing has been written to a camera. No cable has been plugged in for a test.

---

## 1. Scope decision (settled)

**Hardware work targets the original KC02 only.** The X8D and Z-12 build and are
covered by emulator tests; they are not release targets and no hardware effort
goes into them.

Two things follow, and both relax constraints that were tight:

| | Original KC02 | Z-12 (no longer binding) |
|---|---|---|
| Application RAM (`0x02000000` up) | **7 MiB** (`stack top 0x027ffffc`) | 1 MiB |
| Largest erased, sector-aligned flash window | **992 KiB** | 264 KiB |

So memory is no longer scarce, and the flash layout is a single constant rather
than a cross-board minimum. The remaining Z-12 value is regression coverage: its
sensor switch and the X8D key ladder both caught real bugs in the shared core.

---

## 2. The two workstreams

| | Stock emulation | Luma |
|---|---|---|
| What | Run the unchanged vendor firmware in QEMU | New freestanding firmware |
| Docs | `HANDOVER_EMULATOR.md`, `docs/EMULATOR_PLAN.md` | `custom-firmware/{README,DESIGN,BRINGUP}.md` |
| Policy | Never patch guest instructions or inject application state | Our own code, own HAL |
| Status | Complete for the original, alternates, capture, printing | RAM milestone plus service cores |

`custom-firmware/` is the new tree. The emulator lives in `tools/emu/` plus the
machine sources it owns in `tools/emu/qemu/ax3292/`.

---

## 3. What exists and is verified

**Luma** (`custom-firmware/src/ui.c` and friends): three screens (camera, gallery,
list settings), RGB565 320×240, antialiased text and geometry, boot/shutter
animations, SD-loaded theme (colours plus two BMPs), capture to BMP, gallery,
shared no-print/dither/grayscale policy. Builds for three boards from one core,
bounded by the *original's* 7 MiB now that Z-12 is out of scope.

**First-stage bring-up** (`custom-firmware/bringup/`): a recovery gate and a GDB
monitor that run before any UI, entering at the factory application entry
(`0x0200046C`). Preserves the BLDR header, boot code, SDRAM initializer and
resources. `tools/factory.py` refuses to change a byte outside the application
copy span, and refuses the install filenames.

**The monitor**: trap-based breakpoints and single-step. Own vector table at a
fixed `EVBA`, 16-byte slots, full context save. `src/or1k_decode.c` decodes
control flow, so a step traps the *successors* of the current instruction.

**USB**: three interfaces on one composite device.

| Function | Interface | Class | Endpoints |
|---|---|---|---|
| Mass storage | 0 | 08/06/50 | EP1 IN/OUT |
| Vendor debug (GDB) | 1 | ff/4c/01 | EP2 IN/OUT |
| WebUSB update | 2 | ff/55/01 | EP3 IN/OUT |

Mass storage speaks BOT plus a SCSI subset; the LUN reports **no medium** until
`sd_handoff` gives the card to the host. WebUSB carries a staged container for
**both firmware and themes**.

**Update paths**: `src/update.c` (validated A/B engine), `src/update_sd.c`
(`FIRMWARE.IMG` discovery, `READY` vs `OK`), `src/webusb.c` (staged container,
per-file CRCs), `src/theme_bundle.c` (theme validated as a set, committed
images-first).

### Verification, and what each kind proves

- `tests/test_custom_firmware.py`: real C core compiled with UBSan.
- `tests/test_msc.py`, `tests/test_webusb.py`, `tests/test_update_sd.py`,
  `tests/test_theme_update.py`: protocol peers that act as the USB host, over
  real FAT16 card images. These are conversations, not calls into my own helpers.
- `custom-firmware/tools/smoke.py`: Luma in QEMU on all three profiles.
- `custom-firmware/tools/bringup_smoke.py`: relocation, checkpoint cycles, and
  the in-guest trap/step self-test, which reports `failures: 0x0`.

Evidence lives under `~/.cache/kc02-emulator/`, notably `luma-aa-final`,
`msc-selftest2`, `msc-update3`, `luma-after-webusb`.

---

## 4. Not done, and not claimed

1. **No cold boot.** The stage reuses the factory clock, SDRAM and pin
   configuration. Nothing brings silicon up from reset.
2. **No NOR driver.** The update engine validates and refuses; it has never
   written flash.
3. **Nothing has enumerated.** QEMU does not model the USB controller. Two and
   three independent bulk endpoint pairs is an SDK assumption; the stock firmware
   demonstrates only EP0 and EP1.
4. **No hardware at all.** No camera touched.
5. Cache maintenance for trap words is written to the vendor contract but
   unverified. HP shadow-bank capture is absent. Ctrl-C cannot halt a running
   target. The boot-prefix comparison has no trusted snapshot, so the recovery
   gate stops at `RECOVER_PREFIX_UNAVAILABLE` (code 6).

---

## 5. Measured facts from the last session (do not re-derive)

These cost real effort. They are measurements, not documentation.

### Flash layout per board

| Board | Last non-`0xFF` byte | Largest erased aligned window |
|---|---|---|
| original | `0x3071FF` | `0x308000`–`0x400000` = **992 KiB** |
| X8D | `0x1CEFFF` | `0x1CF000`–`0x400000` = 2244 KiB |
| Z-12 | `0x3FFEFF` | `0x3BD000`–`0x3FF000` = 264 KiB |

`0x307200` is **not** sector-aligned, so erasing the sector containing it would
also erase the last 512 bytes of assets at `0x307000`–`0x3071FF`. Always align
the window up from the asset end, and confirm the erase size on hardware by
erasing one candidate sector and checking `0x307000`–`0x3071FF` is unchanged.

### Nothing validates the application payload

- No CRC32 polynomial appears anywhere in the boot or app region of the dump.
- The header's additive checksum covers only the first 512 bytes (it sums to 0).
- The validated surface is: `BLDR` magic, `0x55AA`, the two `0x01234567` markers,
  `SFAT` at `0x304`, and that checksum. The updater's checks are the same.
- The SDK boot source's copy is a dumb sector loop with no checksum
  (`flash_to_psram` in `ax32xx/src/ax32xx_int.c`).

**Two consequences.** A tiny loader in the app region's first sectors boots with
anything after it, which makes a recovery-first trampoline viable. And an
interrupted write to the app region is undetectable: the boot chain copies
garbage and jumps into it.

Caveat: that is the SDK's source. The repo matched 60 boot symbols with high
shape fidelity, but constants differ per board. Strong evidence, not proof for
our dump. Settling it means diffing the dump's copy loop against the vendor's
`ax32_platform_demo/output/ax329x_sdk.lst`.

### The SFAT table (original)

At the documented `0x0D3200`, 97 monotone 8-byte `{u32 offset, u32 size}` entries,
highest asset end `0x231D69`. Guessing the offset from the app end finds garbage;
use the documented address.

### The emulator cannot answer boot-chain questions

`tools/emu/qemu/ax3292/ax3292.c` copies only the *boot region* into low RAM
(`flash[0x200:]`, capped at 64 KiB) and loads the application through `-kernel`.
The BLDR chain is **modelled, not executed**. Anything about header validation,
the copy span or the handoff has to be answered from the dump or on hardware.

---

## 6. Hardware plan (original KC02)

Ordered. Each step gates the next.

### Step 1 — RAM-only peripheral readback (read-only, no flash)

The stock firmware boots and *is* the bring-up. A payload runs inside it from a
measured-free RAM window, using the firmware's own USB and I-cache helper.

```sh
python3 tools/kc02_usb.py selftest                 # offline guard check
lsusb | grep -Ei '0219|1908'                       # identify the USB mode
python3 docs/hw/execute_payload.py --payload camprobe2
```

Purpose: replace my QEMU-derived register guesses with measurements. Read back
the DE geometry and strides, the panel registers, the ADC channel and thresholds,
the CSI setup, the SD divider, and the flash ID. Payloads available:
`echo`, `mode`, `camprobe`, `camprobe2`, `sensorprobe`.

Known risk: `tools/kc02_usb.py probe` (the `BLDR` at `0x01FFDA04` flash-window
test) has a documented bus-error risk and may need a power-cycle. Everything else
in this step is read-only.

### Step 2 — flash driver test in the tail (writes, but reversible by the device)

The 992 KiB window is erased and unread. Program a sector, verify it, erase it
back to `0xFF`, verify that, then diff the whole 4 MiB against the dump. Even a
power failure mid-test is harmless.

Six guards, all cheap:

1. **Prove the mapping before writing.** Read the BLDR header, the app start and
   the SFAT marker, and compare against the known dump. Any disagreement means
   the addressing is wrong: refuse.
2. **One guarded write primitive.** Every erase and program goes through one
   function taking `(offset, size)` that refuses anything outside a window
   computed from verified readback. The dangerous thing should be inexpressible.
3. **Erase-size arithmetic from the chip**, discovered by erasing one candidate
   sector and confirming the last 512 asset bytes are untouched.
4. **Erase only what reads `0xFF` on entry; restore to `0xFF` on exit.** The test
   is idempotent, so re-running after a crash is safe.
5. **Full-flash diff afterwards**, outside the declared window. A difference is a
   loud failure.
6. **Run it under the stock firmware** via the RAM payload path, so a hang is a
   power-cycle.

Plus: fresh power, no auto-power-off, card unmounted, and print the intended
range before touching anything.

### Step 3 — SPI recovery (do this before any irreversible write)

The user's CH341A path currently bugs out from interference; lifting the chip is
reserved for later. **This is not required for steps 1 and 2**, which is exactly
why they are ordered first.

### Step 4 — recovery-first trampoline, then install

Real firmware lives in the reversible tail; the app region holds only a small
loader that validates the tail payload's CRC, boots it, and otherwise exposes
recovery. That makes the install the *only* irreversible write, as small as one
or two sectors, and every later update tail-only and therefore reversible.

**Hard rule: the app region stays hard-refused in the driver until step 3 is
done.** No exceptions, because nothing in the boot chain would notice a
half-written application.

---

## 7. Reference values needed on the bench

Every board's `entry`, `stack-top`, `vbg-addr`, key/ADC ladder, panel rotation and
sensor are in `tools/emu/profiles.py` (`Profile.options()`, `machine_args()`).

Original KC02: entry `0x0200046C`, stack `0x027ffffc`, VBG `0x222c`,
panel 90°, ADC channel 3, H63P sensor at slave `0x40`, ID `0x0b == 0x48`.
Keys `(657, 510, 252, 776, 386, 12)` = Up, Down, Left, Right, OK, Escape.
Extras `(386, 1024, 1024, 1024, 1024)`. Flash `ef4016`, UID all zeros.
Printer platen phases `(10, 8, 9, 1, 5, 4, 6, 2)`.

Register contracts:

- ADC `0x9170` CON, `0x9174` BAUD, `0x9178` DAT.
- SD `0x9880` status, `0x9884` control, `0x9888` divider, `0x988c` R2 buffer,
  `0x9890/94` DMA addr/len, `0x9898` cmd, `0x989c` argument/response.
- DE `0xc800` frame, `0xc810` geometry, `0xc818` enable, `0xc828/2c` Y + stride,
  `0xc830/34` UV + stride.
- CSI LCD `0xb060/64` Y/UV, `0xb068` stride, `0xb180` size, `0xb008` bit19 latch.
- JPEG in `0xb820/24` for ctrl `0x400`, else `0xb050/54`; JPGA IRQ16, JPGB IRQ22.
- Printer: supply/job `0x8004` bit6, latch bit5; clock `0x800c` bit13, data bit15;
  strobe `0x8000` bit9; PA7 paper active-low.
- Timer `0x9800/04/08`, pending `0x98a8`; free-running µs counter `0x9908`.
- `sd_hw_init` returns the vendor CSD value, which is numerically the card's
  512-byte sector count, not a byte count.

---

## 8. Traps that already cost time (do not re-learn)

1. **ctypes callback lifetime.** C stores the function pointer; Python may
   collect the callback and the next call jumps into freed memory. Every test
   that mounts a card keeps its readers alive in a module-level list. This bit me
   three times, in three different files.
2. **Two byte orders.** `m`/`M` carry memory bytes in address order; `p`/`P` carry
   register values little-endian. Comparing a register read against the numeric
   hex of its value makes a working target look broken.
3. **SCSI CDB fields are big-endian; BOT headers are little-endian.** Mixed, every
   transfer lands at the wrong block.
4. **The composite configuration descriptor is 78 bytes**, so EP0 must stream it
   in two packets. Anything over 64 bytes needs the multi-packet path.
5. **The staged container's entry is 20 bytes.** At 16 the CRC overlaps the last
   three bytes of the 8.3 name and corrupts every extension.
6. **WebUSB details**: `iLandingPage` is `wValue` and `GET_URL` is `wIndex`; a BOS
   needs `bcdUSB` 0x0210; the capability UUID is little-endian.
7. **gcc 4.9 rejects `(Struct){0}`** under `-Wmissing-field-initializers`. Use a
   `static const` zero instance.
8. **The test harness's DMA address must resolve to real bytes.** A token that
   does not resolve tests the wrong buffer and agrees with itself.
9. **`crc32` lives in `src/crc.c`**, not in the theme parser, so a checksum does
   not drag the theme engine in.
10. **The Luma linker script keeps the 2 MiB bound** even though Z-12 is no longer
    a target. It costs nothing and keeps all three building. Relax it only
    deliberately.

---

## 9. Commands

```sh
# Build
python3 custom-firmware/tools/build.py                     # Luma, 3 boards
python3 custom-firmware/tools/build_bringup.py             # original, USB detached
python3 custom-firmware/tools/build_bringup.py --selftest \
  --output custom-firmware/build/bringup-selftest
python3 custom-firmware/tools/build_bringup.py --usb-vid 0x1209 --usb-pid 0x0001 \
  --output custom-firmware/build/bringup-usb

# Test
python3 -m pytest -q                                        # 362 pass

# Luma in QEMU (needs a private create-only card)
python3 custom-firmware/tools/card.py ~/.cache/kc02-emulator/luma.img --demo-photos
python3 custom-firmware/tools/run.py --board original \
  --sd-card ~/.cache/kc02-emulator/luma.img
python3 custom-firmware/tools/smoke.py --output ~/.cache/kc02-emulator/luma-NEW

# First-stage QEMU checks
python3 custom-firmware/tools/bringup_smoke.py --output ~/.cache/kc02-emulator/bu-NEW
python3 custom-firmware/tools/bringup_smoke.py --build custom-firmware/build/bringup-original \
  --card <card with FIRMWARE.IMG> --expect-update-version N \
  --output ~/.cache/kc02-emulator/bu-card-NEW

# Stock emulator regression
python3 tools/emu/smoke.py firmware/original.bin --output ~/.cache/kc02-emulator/stock-NEW

# Hardware (read-only first)
python3 tools/kc02_usb.py selftest
python3 tools/kc02_usb.py probe            # BLDR at 0x01FFDA04; bus-error risk
python3 tools/kc02_usb.py dump-ram --out ~/kc02-hw/ram1.bin
python3 docs/hw/execute_payload.py --payload camprobe2
```

Toolchain: working patched little-endian/no-delay compiler at
`~/.cache/kc02-toolchain/prefix/bin/or1k-elf-`. Do **not** use
`~/.src/ax3292-toolchain/out/prebuilt-or1k-elf/` (unpatched, big-endian).
QEMU: `~/.src/qemu-11.1.2/build/qemu-system-or1k`.
GDB: `~/.cache/kc02-gdb/root/usr/bin/gdb-multiarch`.

---

## 10. Immediately next

The camera is running and attached. Step 1 above is read-only and ready to run.

Suggested order for the session:

1. `tools/kc02_usb.py selftest` and identify the USB mode with `lsusb`.
2. `dump-ram` to a **persistent** path under `~/kc02-hw/` (not `/tmp`; earlier
   sessions lost `/tmp` artifacts on reboot).
3. `execute_payload.py --payload camprobe2` and `sensorprobe`, recording what the
   stock firmware actually programmed, to replace the QEMU-derived guesses.
4. Only then decide whether the `probe` flash-window test is worth its risk.

Save everything to `~/kc02-hw/$(date +%F)/` and record the commands alongside the
outputs. Write the findings back into this file's section 5 rather than only into
the chat, because that section is what the next session reads.
