# Next steps / open work

## 1. Complete the HAL map mechanically  -> **DONE** (toolchain agent + alignment re-run)

**Both blockers fell.** `__sfr` is implemented as a GCC named address space (address space 1, 32-bit
pointers, `l.mfspr`/`l.mtspr` with OR-combined selectors, conservative CC clobber on SPR store) — 308 SFR
checks plus the original 30 checks pass, documented in `/tmp/ax3292-toolchain/SFR-RESULTS.md`. The four
filename-case aliases let **all 239 SDK TUs compile and link**
(`/tmp/ax3292-toolchain/tests/sdk-sfr-probe/ax329x_sdk.elf`, 3375 symbols; the 239 `.o` files live in the
same directory). Note this ELF is a *build artifact, not a flashable firmware*.

**Re-running the alignment over the full object set** (mask bug fixed first — see §6.1):

```bash
cd /tmp/kc02-board-re
python3 sdk_match.py /home/jbaiter/.src/ax3292-sdk/firmware0.96v2_totest/bwlib/*.a \
                    /tmp/ax3292-toolchain/tests/sdk-sfr-probe/*.o
```

-> **312 matches (295 exact >=99% score) over 187 objects; 265 unique labels, of which only 2 were new**
(264 were already applied by the earlier vendor-listing pass). The SDK-fingerprinting well is dry.

**Follow-on that did pay off — ordered gap-alignment.** For every pair of reliable anchors (blob functions
named from the vendor listing) the vendor symbols strictly *between* them were matched 1:1 against the
blob's unnamed functions **only where the counts were equal** (52 such gaps). That yielded **92 new names**,
each carrying an audit comment naming its two bounding anchors; applied via
`ghidra_scripts/Kc02GapNames.java` from `gap_names.tsv`.

**Project totals: 2314 functions, 1919 named (82.9%), 395 unnamed.**

The 395 leftovers are *not* SDK code — they have no vendor counterpart: KC02-specific logic (the
printer/sensor block `0x0205D16C`–`0x02066840` holds 62 unnamed functions against a *single* vendor
symbol), SDK modules the sibling demo build never included, and statics whose code shape differs.
Naming them needs **inference** — string/xref clustering, call-graph/module grouping, contiguous-run
heuristics — not fingerprinting. That is now the main open naming work.

## 2. App layer map  -> in progress (this is the remaining unmapped surface)

- UI state machine: 35-entry dispatch table at file `0x07DF08`, mode callback table `0x07E19C`,
  `ui_mode_request` (CPU `0x02008108`, 28 call sites) — build the mode -> handler table.
- Task/message maps: `0x020C2B40` (mode descriptor), `0x020C2B50` (task map, msgs `0x1A`/`0x23` ->
  print image), `0x020C2FF8` (task map, msg `0x01` -> print file), plus any further maps found by
  scanning the data region for `{u32 msg, u32 handler}` runs.
- Photo pipeline: JPEG encode entry `0x020087E8`, decode/scale `0x020593CC`, thumbnail/rotate block
  `0x02075F00-0x02077500`, image encode module `0x02053000`.
- Games, playback, preview screens; remaining UI resources.

## 4. Ghidra project is self-describing (done)

The shared analysis project now carries the knowledge directly, so future sessions do not need the repo
open to make sense of the image:

- **Structures** (`DataTypeManager`): `kc02_device_node` (0x18), `kc02_adc_key_entry` (8 `[key_id][center][lo][hi]`),
  `kc02_task_map_entry` (8), `kc02_sensor_item` (0x50, with the `(offset,size)` pair fields, inline name,
  id/id_reg/I2C command bytes), `kc02_printer_state` (a single 4-byte counter; the Ghidra type was drawn as a larger struct but the image has exactly one access to `0x020D2EC8`, a store in `print_run_A`, so only the counter is evidenced; see HARDWARE_OBSERVATION.md),
  `kc02_sensor_ops_blob` (0xC80).
- **Typed data** applied at: device list nodes `0x020C259C`, ADC keypad table `0x020C268C`, task maps
  `0x020C2A44` / `0x020C2A64` / `0x020C2B50` / `0x020C2FB4`, H63P sensor item `0x020CB3F0`, H63P ops blob
  `0x020CFD30`, printer state `0x020D2EC8`, `ui_state_dispatch` `0x0207B908`, screen callback table
  `0x0207BB9C`, mode descriptor `0x020C2B40`, and the 100-pair H63P init table `0x020D09B0`.
- **Plate comments** on the key functions (printer motor/head driver, keypad scan/decode, sensor init,
  photo JPEG encoder, `ui_mode_request`, `kc02_upgrade_verify`, boot-mode call site) plus **port-bit
  semantics in the `spr` space** (PORTA/PORTB/PORTE/PORTF, IIC1, USB, AUADC, display, SPI).
- **Bookmarks** (type `KC02`) with the full findings: `overview` (mapping formula, language, caveats, doc
  list), `printer`, `keypad`, `sensor`, `boot`, `tooling` (how to extend: entry heuristic, matcher,
  vendor-map alignment, disassembly commands).
- **Docs copied into the workspace**: `<ghidra artifact>/knowledge/` holds `BOARD_RE.md`, `APP_MAP.md`,
  `SDK_MAP.md`, `BOOT_MAP.md`, `NEXT_STEPS.md`, the three detailed `board-*.md` reports, `sdk_match_all.json`
  and `sfr_map.tsv`.

## 6. Tooling corrections learned (apply them)

1. **Immediate mask**: the load/store mask in `sdk_match.py` was `0xFFE007FF`, which leaves the upper five
   bits of a 16-bit offset unmasked and reports false mismatches (`l.sw -12(r1),r14` vs `l.sw -8(r1),r14`).
   Use **`0xFFFF0000`** for all 16-bit immediate forms (and `0xFC000000` for branch offsets). **Fixed in
   `sdk_match.py` itself** (`l.lwz/l.lbz/l.lhz/l.muli` -> `0xFFFF0000`, `l.mfspr`/`l.mtspr` added; the SFR
   forms matter now that `__sfr` compiles). The corrected implementation and numbers are in
   `/tmp/kc02-board-re/kc02_sdk_align.py` and `map-sdk-align.md`.
2. **Prefer the vendor `.lst` over the `.map`**: `ax32_platform_demo/output/ax329x_sdk.lst` has 2814 symbols
   (1971 in `.text`) with their exact instruction words; the `.map` prints only *global* symbols (it omits
   e.g. `get_fat`, `sync_fs`, and mis-sizes `clst2sect`).
3. **There is also a fully linked vendor image**: `ax329x_sdk.bin` (`.text` vaddr `0x02000000`, load
   address `0x2000`). It is a sibling build of the KC02 image and is the best byte template for diffing.
4. **Symbols with no `movhi/ori` reference** exist (e.g. the `{keymap, widget, 0, 0}` descriptor structs like
   `0x020C433C`, and the game thumbnails `0x0207BBB0`) — they are reached only from data, so a normal xref
   pass misses them; scan for pointer tables instead.

## 6b. Boot section (item 2 of the old list) -> **DONE**

The boot/exception section is now analysed at its true addresses in a separate base-0 project
(`/tmp/kc02-board-re/bootv2proj`), reproducibly extracted with `dd skip=0x370` so that program address ==
boot VMA. All **45 functions + 14 data labels** are named from the vendor listing, and the generic OpenRISC
`default_symbols` that were fabricating functions in that range have been removed from `OpenRISC.pspec`.
Details and the full symbol list: `docs/BOOT_MAP.md`, `docs/boot_symbols.tsv`.

## 7. Custom firmware skeleton (after 1+2)

Write the app layer against the now-labeled HAL, replacing the KC02-specific pieces with our own:
printer driver (`docs/BOARD_RE.md` -> port bits + head protocol), ADC keypad, H63P sensor bring-up
(resource-item model), and our own UI. The platform layer (HAL, FatFs, SD, AVI, USB, ISP/MJPEG, OSD,
RTOS) can be reused from the SDK.


## 9. Naming tracks (2026-10) — see docs/NAMING.md and docs/STRUCTS.md

* **Track 1 (vendor similarity alignment): done.** `seq_align.py` generalised the count-matched gap alignment
  into a scored Needleman-Wunsch alignment against the vendor's linked sibling build; calibration on known
  anchors mean 0.814. Result **+25 names -> 370 unnamed (2314 functions, 1944 named = 84.0 %)**, plus **92 named functions flagged
  as `NAME-TO-VERIFY`** because their vendor counterpart does not match. Measured ceiling: the remaining
  functions are product-specific (best score 0.40 against all 1363 vendor entries), so vendor names do not
  exist for them.
* **Track 3a (struct recovery): pipeline done, application pending.** A synthetic `-g` TU over 34 SDK headers
  yields 186 structs / 1555 members / 413 typedefs (`sdk_types.tsv`); 184 structs were created in the project
  at exact offsets. Next: independent DWARF DIE parser to fix names Ghidra mangled (`fsi_flag.conflictN`),
  then apply the 267 struct-pointer SDK prototypes to blob functions.
* **Track 2 (evidence-driven naming of product code): ready to run.** `name_evidence.py` +
  `name_worklist.tsv`: 337 of 370 unnamed functions have machine-checkable evidence.

## 10. Track 2 — evidence-based naming (2026-10) — see docs/TRACK2_MODULES.md

Delegated to 11 parallel `deepseek/deepseek-flash` worker subagents (one per ~34-function batch, each given
the machine-extracted evidence + decompiled bodies). All 370 proposals validated mechanically (0 rejects,
0 duplicates, 0 name collisions, 0 evidence contradictions). **206 applied** (18 certain + 188 likely) with
provenance comments -> project now **2314 functions, 2150 named = 92.9%, 164 unnamed**. The 164 `guess`-tier
proposals are held in `docs/review_names.tsv` for a stronger-model second opinion, then application.
Subsystems identified: MP3 decoder, two mini-games, software-I2C bit-bang, UI widget pool, photo effect
filters, JPEG/MJPEG codec core, AVI record/playback, board ioctl, `.LST` file list manager, task dispatch.

## 11. Track 2b — second opinion + final naming state (2026-10) — see docs/TRACK2B.md

164 low-confidence proposals re-examined by `openai-codex/gpt-6.1-sol` (16-17 functions per lane):
26 confirm / 60 revise / 78 keep_unk -> **84 more names applied**. Project total
**2314 functions, 2234 named = 96.5 %, 80 unnamed**. The 78 `keep_unk` verdicts
(`docs/keep_unk_final.tsv`) are evidenced rejections: those functions cannot be named from the current
evidence and need caller-chain analysis or hardware observation.

## 12. Hardware observation (2026-10) — see docs/HARDWARE_OBSERVATION.md

Read-only live state read of the attached camera (USB 0219:3280) via the stock vendor command with
CALLBACK_SKIP. Device-verified: the keypad ADC `±0x23` windows; the ten mode descriptors and their labels
(`Power Off`, `Video Recorde`, `Photo Encode`, `Play Back`, `Audio Player`, `Main menu`, `Game menu`, `Game`,
`Setting menu`, `usb device`) with `{label, op1..op3, keymap}` layout and inline `{msg,handler}` maps; message
semantics (`0x0E` enter, `0x0F` exit, `0x1A/0x23` OK, `0x1B/0x1C` up/down, `0x1D/0x1E` left/right, `0x24`
back). Corrected: `0x020CCC8C` is a **runtime overlay** (the image holds a 432-byte curve there) and the live
dispatch table carries only the *active* mode's entries. Found a census gap: the mode maps reference handler
entries that the project merged into a few large functions (`mode_settings_enter` @0x02015DB4,
`ui_request_mode6_arm_b68_c` @0x02015074, `usbDeviceKeyMsgMode` @0x0200D408) — splitting them at the
map-verified entries fixes the census and names each handler. 9 handlers named this way (80 -> 71 unnamed).

## 13. On-device execution harness + remote UI control (2026-10)

`ui_mode_request` can be driven from the host: our payloads execute on the camera under a fail-closed
envelope (pinned firmware signatures, canary, read-back-verified uploads, stock I-cache reset, byte
whitelists). `ui_mode_request(6,0)` reached the main menu and `ui_mode_request(3,0)` reached camera mode,
both confirmed on the device. The request is serviced only after our USB command returns (the payload runs
inside the app's USB-command task), camera mode tears down the USB function, and with the battery connector
broken (USB-only power) camera mode is not sustainable. Mailbox pattern (`0x020DB300`, measured-free span)
captures state across such a drop. Blocker for camera-mode work: fix the battery connector; for flashing:
battery too (update on USB-only power risks a brick).

## 14. USB-powered ceiling reached (2026-10)

Completed while USB-powered: full-image integrity audit (855 040 B read; only 23/417 chunks differ, all in
runtime-initialised regions), structural validation of every typed table against live bytes (ADC windows,
system dispatch, ten mode descriptors with addresses for all lifecycle ops), on-device code execution
harness with payloads and remote UI control via `ui_mode_request`. The sensor item/ops/tables are *flash
resources* copied to `0x020E4BA0`/`0x020E32A0`/`0x020D1EA0` during bring-up, not RAM globals.
Later camera analysis corrected the dispatch interpretation: `0x020CCC8C` is the primary vector populated
from the installed window map, not a system-only table. See [CAMERA_SCREEN.md](CAMERA_SCREEN.md).

Pending on hardware: `docs/hw/payloads/sensorprobe.c` (sensor identity via the firmware's own
`sensorAutoCheck`), then camera-mode observation and the byte-identical rebuild flash. Blocker: the unit
enumerates and then drops within seconds to minutes on USB-only power - fix the battery connector first.

## 15. Sensor identity CONFIRMED on hardware (2026-10)

`sensorprobe` ran successfully: `sensorAutoCheck` returned the real ops `0x020E32A0` (the test-pattern
fallback `0x02083CB0` would mean failure), the probe register `0x0B` read back `0x48`, and the driver's RAM
copies match the flash resources byte-for-byte. The H63P identification is now device-verified: reg `0x0B`
== `0x48`, 7-bit I2C `0x40`, 100-pair init table. Remaining hardware work is blocked on the battery
connector: camera-mode observation (mailbox/hook pattern) and the byte-identical rebuild flash.

## 16. Print test for the 383-vs-384 head question (2026-10) - pattern ready, needs the printer

`docs/hw/print_test/` holds a purpose-built pattern (`t384x384.jpg` + 384x320 / 672x384 variants and PNG
previews) and `README.md` with the procedure and an interpretation table. The head shift-out loop emits
`rows-1` = 383 clocks and never drives bitmap index 0, so the pattern is built to make a missing edge
column visible at ruler scale: a 2-px edge frame (missing left line == column 0 dropped), 8-px tick ruler
(paper-edge-to-first-tick is 0.125 mm if column 0 prints, 1 mm if not), control lines, orientation block.
Print from playback mode (mode 4, message 0x1A), which can be requested remotely with
`execute_payload.py --payload mode --mode 4`.

## 17. Printer 383-vs-384 question RESOLVED on hardware (2026-10)

Printed a test bitmap directly from RAM (payload `docs/hw/payloads/ramprint.c`; canvas + `kc02_print_run_A`,
no card/gallery/mode change, status 0 = ok). Five 1-dot lines at head dots 0, 4, 8, 12 and 383 all appeared
(4 left + 1 right), so **both extreme columns are driven, dot order is left-to-right, and no column is
dropped**; the 383-clock loop only causes a 0.125 mm one-line smear in the unclocked column, invisible in
practice. Remaining battery-gated items: solid fills (head current), camera mode, and any flashing.

## 18. Print-density budget measured (USB power) — text/fills need the battery (2026-10)

> **SUPERSEDED by §21.** These are the USB-only-power results, taken while the battery
> connector was broken. After the repair the same staircase prints all 16 bands and
> full-width bars cover all 384 dots, so the ceiling below was the power source, not the
> head. Kept as the record of how the limit was isolated. See
> [hw/post_repair/README.md](hw/post_repair/README.md).

Established by separating the axes (position vs count): 4-dot marks print anywhere across the head with
uniform pitch, but 12/24/48/72/383-dot-per-line features fail **even when left-aligned**, and the status code
(14 = charging) already yields the maximum strobe bias. So on USB-only power the head marks at most
**~8-11 dots per line (~2 % of the head)** - a power/drive limit, not a positional or firmware-throttling one.
Denser lines (12+, and the 48/72/383-dot patterns) never reach the paper; 1-dot lines and 0.125 mm features
print fine. So the printer is usable but power-starved: **photos, fills and normal text need the battery
connector fixed** (as do camera mode and, for safety, flashing). Fine-grained print-quality work (density
ramp, dithering, a real image) is therefore queued behind the hardware repair.

## 19. Camera screen rebuilt offline

[`camera_screen/camera_screen_model.c`](camera_screen/camera_screen_model.c) now implements all 33
camera-region functions, including the key handlers and capture/save/print orchestration, while importing
stock framework/drivers. The target ELF links without undefined symbols; the 48-byte widget table matches
the image and the new keymap points to the rebuilt handlers. Host tests pass 6,018 assertions, including
5,956 differential controller-state cases against five byte-checked original handlers with callees mocked.

Build and test commands: [camera_screen/REBUILD.md](camera_screen/REBUILD.md). The module is 10,768
bytes, larger than the existing 5,632-byte RAM slot. Its example link address is not verified free RAM.
Next: repair power, reserve memory, validate ABI/ownership and test lifecycle/preview/exit with rollback.
No camera-module upload, mode hook or flash modification has been attempted. Preserve the old camera
region because non-camera code still calls its shared file-opening helper.

## 23. Documentation audit (2026-10)

Audited every document against the findings in the repo; results and the remaining disagreements are
in [DOC_AUDIT.md](DOC_AUDIT.md), and [README.md](README.md) is now the map of which docs are current
versus historical.

Fixed: the retracted JieLi/pi32 platform claim and the wrong Ghidra setup in `FIRMWARE_NOTES.md` (now
bannered as partly superseded); a duplicated section in `HARDWARE_OBSERVATION.md`; USB-only print
limits presented as current fact; the claim that status 14 means a battery is absent; two broken
links; the stale firmware md5 quoted in four documents; a diverging second copy of the observer.

Both disagreements the audit found are now settled by decision: the print drivers load **383 bitmap
columns starting at index 1** with the dot order **mirrored** (bitmap index 0 = the paper's right edge),
trusting the reversing work over the earlier paper-only reading; and **printing while charging works as
long as the battery is connected**, so status 14 means charging and never "battery absent". What the
unused shift-register position carries still needs an electrical check, and whether the old md5 is an
earlier dump of the same camera or a different revision remains unconfirmed.

## 25. The newer units' new features located (2026-10)

**The selfie sensor.** Both new builds add a sensor-ID dispatch the original lacks: the X8D reads an
ID at `sensor_base+0x40` and the Z-12 at `+0x50`, and both handle **`0xA0` and `0x9B`**, while `0x48`
is the original `H63P` front sensor. A search of the original's whole sensor area finds no `0xA0`/`0x9B`
comparison, so those IDs are the back camera. Support is added as per-ID `{register, value}` tables
written through the existing helper, so the sensor framework is unchanged.

**The row-based menu.** It is built on the original's grid UI rather than a new one: the region after
`ui_grid_init` uses `win_find_child_by_id` / `winSetVisible` / `winSetStrInfor`, child widget IDs 5..9
act as rows, a page loader swaps pages, and the mode entry blocks in `XMsgQPend`. Six adjacent 24-byte
`return 0` functions are placeholder table slots, not logic.

**Where the new code is.** Propagated names double as a relocation map, so gaps between anchor pairs
expose inserted code: 32 % of the X8D and 45 % of the Z-12, concentrated in 10-11 regions. The Z-12's
92 KB block next to `R_stringGetDataAndSize` is its credit/UUID/Bluetooth-print subsystem. Tool:
`tools/sfat/new_regions.py`.

## 24. The two newer units imported and named (2026-10)

MYSTILUCK X8D (`Ax3295B-V3.0`) and HMOLLIMT Z-12 are now Ghidra programs in the shared project
alongside the original, with 2915 and 3447 functions and 883 / 910 names propagated from the original
by masked-opcode match. Full detail and the verification in [NEW_BUILDS.md](NEW_BUILDS.md).

Two things worth carrying forward for any future build. **Auto-analysis is not enough for a raw
import**: with no entry points it found 426 functions of ~2300, and its 112 analysis options are
identical to the original's, so the original's function set came from scripted discovery rather than
analyzer settings. Entry candidates have to be decoded from the instruction encoding
(`tools/sfat/or1k_function_starts.py`). And `l.jr rB` keeps `rB` in bits 15..11, not 25..21 - reading
the wrong field reports zero returns and makes an image look like it has no epilogues.

Also corrected: the X8D application loads from file `0x2200`, not `0x2600`, so its file-to-CPU base
is `0x01FFDE00` and the SPI reader is `0x02037404` (not `0x02037004`). Every X8D address computed
with the original's `0x01FFDA00` base is `0x400` low. Dumps were never affected because the tool
locates the reader by signature.

## 22. USB feature feasibility: WebUSB and printer class

Both are feasible; neither is an SDK option, because the vendor stack has no BOS, WebUSB or
printer-class support at all. Analysis, descriptor addresses and the exact BOS/UUID requirements are
in [USB_FEATURES.md](USB_FEATURES.md).

Verified starting point: three device descriptors selected by mode (`1908:3283`, `1908:3282`,
`0219:3280`), two configuration blobs, a 5-endpoint controller (EP0 + EP1-EP4), and the `0xCD`
command is a private SCSI opcode on the mass-storage interface rather than a vendor-specific
interface.

* **WebUSB** needs a new vendor-specific (`0xFF`) interface plus a BOS descriptor with the WebUSB
  platform capability, and `bcdUSB` raised to `0x0210`. The existing mass-storage interface can
  never be the target: Mass Storage is on the WebUSB protected class list and the OS driver owns it.
  The serious work is the trust boundary - today all safety lives in the host harness, and a browser
  tool would move it onto a web page.
* **Printer class** is easy descriptor work, but the firmware has no page description language,
  font engine or rasteriser, so the host must rasterise. ESC/POS raster (`GS v 0`) plus a
  `GET_DEVICE_ID` string advertising ESC/POS compatibility is the pragmatic route; driverless
  printing is IPP-over-USB and out of scope.

Both are blocked on the same thing: a verified update and recovery path, because a malformed
descriptor set makes the device un-enumerable.

## 21. Battery repaired: printing works across the full head (2026-10)

Battery connector repaired. The USB-only supply ceiling is gone: the 4..64 dot staircase prints all
16 bands, and three 6-line full-width bars print across all 384 dots. All built-in patterns now match
an independent canvas model exactly. Details and results: [hw/post_repair/README.md](hw/post_repair/README.md).

Printing a JPEG straight from the card works from a RAM payload:
`open(name, 1) -> fs_size(fd) -> kc02_print_file(fd, energy, layout, p6) -> close(fd)`, with
`open 0x0206C5B8`, `close 0x0206C680`, `fs_size 0x0206CAF8`, `kc02_print_file 0x0205ADF0`.
Stock photos and generated art cards all printed recognisably (status 0). `kc02_load_print_image`
returns 1/2 for allocation failure, 3 when the decoder rejects the JPEG, 4 on decode timeout, 5 on
scaler failure. **The decoder needs 3-component YCbCr 4:2:0; single-component grayscale fails with 3.**
The stock firmware caches the FAT directory, so new host-written files need a power cycle before
`fs_size` reports them.

Two payload rules learned: the entry function must sit at the code-image start
(`__attribute__((section(".entry")))`) because the RAM dispatcher jumps to `CODE_ADDR`, and mutable
statics are fine when `.bss` is linked inside the code window and every global is initialised at entry.

A 20 cm full-width maze with a treasure chest and a dot-resolution card both printed. Their canvases
were verified on-device against a host model by FNV-1a hash before printing. Feed timing is about
11 ms per line (roughly 88 lines/s). The printed resolution card was measured: 48 mm wide, 10 x 10 mm
squares, precise centimetre scale, and every line pair from 1 dot up resolves. That confirms
**8 dots/mm across and 8 feed lines/mm along (203 dpi, 0.125 mm pitch in both axes)**.
A printed checkerboard ladder adds the practical limit for patterned content: a 1-dot checker
(0.125 mm cells) saturates to solid grey while 2-dot cells (0.25 mm) are the smallest the eye
resolves, so halftoned content is limited to about 4 cells/mm even though isolated marks reach
203 dpi.

## 20. Main menu decompiled and rebuilt offline

[MAIN_MENU_SCREEN.md](MAIN_MENU_SCREEN.md) records all 15 menu-region bodies, 54 raw exports,
the 48-byte widget table, six routes, background JPEG and six selection BMPs. The readable controller
links as a 2,800-byte module; 1,101 host-mock assertions pass. No hardware execution or hooks applied.

Normal icons live in the JPEG, not six sUI image widgets. Custom appearance needs both that background
and the selection tiles. Blink message 0x09 normally arrives after more than 500 XOS ticks, despite the
handler's 300-tick guard. Review the retained allocation-failure scratch leak and reserve module memory
before integration. Commands: [main_menu/REBUILD.md](main_menu/REBUILD.md).
