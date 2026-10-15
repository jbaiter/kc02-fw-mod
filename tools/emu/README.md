# AX3292 emulator tooling

The patched QEMU runs the original KC02 colour menu and camera UI with a fake
sensor and keyboard input. See [the runtime guide](qemu/README.md) for build
instructions, controls and limitations.

```sh
python3 tools/emu/run.py firmware/original.bin --interactive
```

## Alternate builds

The launcher detects the complete SHA-256 of the original, MYSTILUCK X8D and
HMOLLIMT Z-12 dumps. Use `--profile x8d` or `--profile z12` for a modified dump.
The profile supplies application bounds, entry/SP, ROM calibration and board
wiring; it does not modify guest instructions or application state.

```sh
python3 tools/emu/run.py firmware/hmollimt-z12-10da5dbf.bin --interactive
python3 tools/emu/profile_smoke.py firmware/hmollimt-z12-10da5dbf.bin \
  --selfie --output "$HOME/.cache/kc02-emulator/z12-ui-new"
python3 tools/emu/profile_smoke.py firmware/mystiluck-x8d-8e0636e1.bin \
  --capture --output "$HOME/.cache/kc02-emulator/x8d-photo-new"
```

Both alternate builds pass cold boot, native menu navigation, moving preview,
return to menu and preservation of fixture JPEGs on a private SD card. X8D also
passes capture, strict JPEG decoding and return to menu. **Z-12 has the second
sensor:** Tab aliases its native Right key and switches cameras in the camera
view. The firmware drives PE11; the model selects independent BF3A03 register
banks and vertical versus horizontal colour bars with a separate white patch.
Switching back restores the first pattern. Sensor orientation follows register
`0x1e`.

[Front preview](../../docs/images/emulator/z12-front.png) ·
[selfie preview](../../docs/images/emulator/z12-selfie.png)

| Keys | X8D | Z-12 |
|---|---|---|
| Left / Right | Previous / next | Previous / next |
| Up / Down | Aliases Left / Right | Change menu row |
| Enter / Escape | Confirm / back | Confirm or shutter / back |
| Space | Shutter | Shutter |
| P | Native ADC 788 (same as Enter) | Native print key ADC 133 |
| F | Native function key ADC 132 | Unwired |
| Tab | Unwired | Native Right ADC 238; camera switch |
| M | Unwired | Native function key ADC 704 |

Z-12 saves strictly decodable 4096×3072 and 640×480 JPEG packets, but the
post-capture input/return workflow still fails. `--capture` deliberately fails
that regression rather than reporting capture support. Switching is verified
before capture; saved selfie images and both alternate printer paths remain
unverified. The Z-12 flash identity matches its dumped JEDEC ID/UID; its optional
OpenImage credit check still fails. We do not override that check.

## SPR addressing (the thing to get right)

Every peripheral on this chip is reached through OpenRISC special-purpose
registers, not memory. `l.mfspr`/`l.mtspr` take a 16-bit number that is the OR of
a base register and an immediate, so a constant access looks like
`l.mfspr rD, r0, K`.

The vendor compiler emits, for a `__sfr` object at byte address `A`, the number
`A` **verbatim** (verified against the reconstructed toolchain: `0x9244` becomes
`l.mtspr r0, r3, 0x9244`). The vendor header therefore works as the number table:

```
SPR = (group << 11) | (index << 2)
```

Two traps this tooling encodes:

* **Group 0 is not the OpenRISC spec map.** A probe on real hardware confirmed
  `SPR_SR = 0x0018`, and `0x11` (the spec SR number) is undefined on this core.
  The vendor's own `ax32xx/reset.S` uses `l.mfspr r9,r0,32` for EPCR and
  `l.mfspr r9,r0,64` for ESR, while `ax329x.h` labels `0x40` EPCR and `0xC0` ESR.
  `sfr_map.py` overrides the header here and records confidence per entry.
* **Aliases are common.** `ax329x.h` defines 1089 names over 1065 numbers.

## Tools

| Tool | Purpose |
|---|---|
| `sfr_map.py` | Parse `ax329x.h` into `SPR -> name/group`; look up a number; emit the table |
| `sfr_map.tsv` | Checked-in table (`name`, `spr`, `group`, `confidence`), regenerate with `--sdk` |
| `spr_census.py` | Static census of `l.mfspr`/`l.mtspr` sites in a dump |
| `run.py` | Slice the application out of a dump and run it headless or in an interactive SDL window |
| `smoke.py` | Verify cold boot, keypad navigation, fake-camera motion and return to the menu |
| `profiles.py` | Full-hash boot handoffs and board wiring for the three dumps |
| `profile_smoke.py` | Alternate-build native UI, optional selfie/capture and SD-preservation regression |
| `capture_smoke.py` | Capture two photos through the firmware, then verify saved JPEG packets and viewing across cold boots |
| `printer_smoke.py` | Verify firmware dither/grayscale head output and missing-paper behavior, plus capture/viewer persistence |
| `sd_read.py` | Read FAT16 files and separate JRX JPEG packets from a stopped emulator's card |
| `qemu/` | The QEMU patch, its build script and notes |

```sh
# regenerate the checked-in map from the vendor SDK
tools/emu/sfr_map.py --sdk /path/to/ax329x.h --emit tsv > tools/emu/sfr_map.tsv

tools/emu/sfr_map.py --lookup 0x9244
tools/emu/spr_census.py firmware/original.bin

# build QEMU once (~2 min on 16 cores), then run a dump
tools/emu/qemu/build-qemu.sh
tools/emu/run.py firmware/original.bin --timeout 15 --trace-limit 40000
```

## Census caveat

The census only resolves accesses whose base register it could track as a
constant through a short linear scan. On stock `original.bin` it resolves about
57% of the 4760 access sites: 58 SPRs with ten or more hits carry most of the
traffic (status, clock, RTC, USB, LCD, PIC, flash), and a long tail of
single-hit numbers is noise from constants that were not SPR addresses. Treat
the ranked head as the device backlog and confirm it with a runtime trace once
the emulator runs.

## Status

The original firmware displays its menu without an SD card or button press.
Short arrow-key presses change the selection; Enter opens the camera view,
which shows fake CSI colour bars under the firmware's OSD. Escape returns to
the menu. The native SDL session and three cold headless smoke runs passed.

We load the application and supply an explicit ROM calibration handoff. The
model does not patch firmware instructions or seed application UI/card state.
[The runtime guide](qemu/README.md) records the corrected interrupt, DMA and
scanout models. Photo capture now persists through SD DMA and works in the
original viewer across cold boots. Pass `--printer-output DIR` to export thermal
head PNGs, including the firmware's grayscale/dither output. Dither, grayscale
selected through the settings UI, and no-paper regressions passed. The PNG models
strobe duty, not calibrated thermal physics. Video recording, USB and audible
audio remain unverified.
