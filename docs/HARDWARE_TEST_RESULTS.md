# KC02 hardware nights — results (2026-10-01/02)

Validation of the offline RE + toolchain on live hardware. Firmware backup md5
`7236c4c1bd3f12b1d85d33a9bebdf510` untouched throughout. All modifications RAM-only.

## Night 1 — the dump and the architecture

| Test | Result |
|---|---|
| `probe` 0x01FFDA04 | device faulted → **flash is SPI-only, no mmap window** (Route B it is) |
| `canary` | PASS — RAM executable, I-cache coherent |
| `flash-read` 4 MB | COMPLETE via hand-built SPI shim (`~/kc02-hardware-2026-10-01/flash.bin`) |
| dump vs backup | 8 bytes differ, one SFAT counter run (0x30701C..0x0x3071FC) — pipeline bit-exact |
| `usb-hoist` | PASS — USB alive across all mode changes |
| `child-lock` | PASS — ↑ then POWER gate works live |
| photo + print while tethered | PASS |

Night-1 crash post-mortem: the first combined application died on `jalr 0`
(zero exit slot in a mode record reached by a transition). Fixed live.

## Night 2 — the text mystery and the LUT gate

### Laws of this firmware (all established by experiment)

1. **The USB/SD screen (mode 10) is terminal.** Its stock exit
   (`0x0200D450`) powers the USB interface down and continues toward
   power-off. **Unplugging while on the SD screen shuts the camera down**;
   unplugging from any other mode is safe.
2. **Printing is refused while charging** — the message is
   "Aufgeladen, nicht verfügbar" ("charging, not available"). Unplug to print.
3. The battery gauge lies under USB load (1 bar plugged / 3 bars unplugged =
   voltage sag artifacts).
4. While USB mass-storage is serviced, printing through the UI can fail
   (SD contention) — the hoist's per-frame BOT keeps MSC alive.

### The disappearing text/HUD — root cause chain (6 wrong theories, 1 truth)

Suspects tried and cleared: usb-screen flag `0x020CCD8C` (red herring),
skipped `mainmenu_enter`, record-copy clobber at `0x020D9118+`, panel-feed
re-assert in the frame stub, resource-set variant, cache-invalidate ghost,
battery. The real mechanism: **the record-replacement unlock path itself**.
The winning architecture needs no record surgery at all:

> **unlock = `usb-hoist` + `ui_mode_request(N)`** (poke `0x020CCD50` ← mode).
> Real records, real enters, real text/HUD everywhere, USB alive via the hoist.

Validated live: text + HUD + USB + shutter all work simultaneously.
One genuine stub bug was found and fixed en route: the frame stub called
`bot_service_wrapper` before the menu frame fn without reloading the
walker-supplied `record->arg` in r3 (walker convention: `frame_fn(record->arg)`
at `0x020082a8..0x020082bc`). The fixed stub saves/reloads r3.

### The LUT art — root cause

The stylize (`img_stylize_gradient_lut 0x02022434`) reads the 63×63 u32 table
at `0x0207C640` (a real asset — 764 nonzero entries, asymmetric pyramid — NOT
a zero pad) and it is the table's only accessor in the whole firmware. All
uploads were correct and verified — but the stylize **never ran**: its gate is
`byte[0x020D41C7] ∈ {7,8}` (the shutter-FSM print-style states), and the byte
sits at 0 in every ordinary capture. The printer's own halftone did all the
"dithering" seen before this discovery. `flag = (byte==8)` selects UV-flatten
(grayscale) vs chroma-keep.

**The recipe (validated live — black page with white edges!):**

1. `usb-hoist` (USB survives mode changes — without it, any transition out of
   mode 10 takes the USB interface down)
2. `pending=3` (camera mode; kills the SD screen; the mode entry resets the
   FSM byte)
3. force `byte[0x020D41C7] = 7` AFTER the entry (the OK key/mode entries cycle
   it away otherwise)
4. shutter → stylize runs over the injected table → black-with-white-edges
5. unplug (from a non-SD-screen mode!), then print (charging law)

The LUT lives at `0x0207C640` = file 0x7EC40. Style formulas:
invert `31-v` (chalkboard), stencil `(v>15)*31`, gamma `v*v//31`, noise
`clamp(v±5)`. Upload with the **cache-clean** callback (the invalidate variant
may drop dirty lines on this write-back cache — unverified but avoid).

## Open items (next session)

- Rewrite `unlock-usb` as `usb-hoist + --mode N + --style N` (gate force
  included). The record-replacement path retires.
- Selftest: two checks still assert the old 5-write plan ("plan has five
  writes", "plan order") — update for the 6th write and header-only record.
- `kc02_unlock.py plan-persistent` needs rework: `0x0207C640` is the LUT
  asset, NOT a free zero pad — never host stubs there.
- PRINT_PIPELINE.md §flag: correct to "flag = (byte==8): 0 → UV flatten
  (grayscale), 1 → chroma keep; gate = byte ∈ {7,8}" (see this file).
- Poke-guard: expose `0x020D41C4..0x020D41C8` (done) and the LUT window
  (done); the LUT playground webapp becomes trivial now.
- Charge-battery/printer interplay doc; the detach-shutdown handler address
  hunt (the SD-screen unplug path) for a possible safe-detach patch.

Artifacts: `~/kc02-hardware-2026-10-01/` (flash.bin, ram1.bin, lut_chaos.bin,
lut_speckle.bin).
