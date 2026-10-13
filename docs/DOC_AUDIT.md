# Documentation audit (2026-10)

Checked every `.md` that documents this firmware against the findings recorded in the repo, plus the
scripts under `docs/`. Method: a coverage grep for each known finding, a relative-link check, a
duplicate-content scan, and a read of the oldest batch of documents.

Canonical image: `firmware/original.bin`,
`sha256 218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5`.

## Fixed in this pass

| Issue | Where | Fix |
|---|---|---|
| Document asserted the **retracted platform** (`JieLi AC2546`, `pi32 core`) and told readers to import as `pi32:LE:32:default` with `ghidra-jieli`. The SoC is **AppoTech/Buildwin AX3292, little-endian OpenRISC OR1K, no delay slots**. | `FIRMWARE_NOTES.md` | Correction banner at the top; the file is now marked partly superseded with pointers to the current docs. History kept. |
| A whole section was duplicated (same header, same table, twice). | `HARDWARE_OBSERVATION.md` | Removed the duplicated block, kept both unique subsections (the axis-separation analysis and the staircase measurement). |
| USB-only power results read as current fact: "text, fills and any real image require the battery", "12, 24, 48, 72 and 383 dots per line do not print". These were taken while the battery connector was broken and are **resolved** after the repair. | `HARDWARE_OBSERVATION.md`, `NEXT_STEPS.md` §18 | Superseded notes added, pointing at the post-repair results. |
| "After the repair that code should change once a battery is detected" implied status `14` meant battery-absent. It does not: the repaired unit printed ~15 jobs at status 14. | `HARDWARE_OBSERVATION.md` | Corrected to state that status 14 is not a battery-absent indicator. |
| Two broken relative links (`../HARDWARE_OBSERVATION.md`, `../PRINT_PIPELINE.md` from a directory one level deeper). | `hw/post_repair/README.md` | Repointed to `../../`. |
| Four documents plus `xref_db.json` quoted `md5 7236c4c1…` as the firmware hash; the canonical image has a different md5 and a sha256 that all tooling pins. | 4 documents | Hash note added to each; details below. |
| `docs/observe_state.py` was a second, diverging copy of `docs/hw/observe_state.py`. | both | The stray copy is now a shim that runs the canonical one, so the two can no longer drift. |
| No entry point explaining which docs are current. | `docs/` | Added [README.md](README.md) with a current/historical split. |

## Open questions the documents disagreed about - both now resolved

> **Resolved 2026-10.** (1) Columns: trust the reversing work - the print drivers load **383 bitmap
> columns starting at index 1**, so bitmap index 0 is never loaded, and the dot order is **mirrored**
> (bitmap index 0 = the paper's right edge). The earlier paper-based "no dropped column" and "no
> mirroring" conclusions are superseded in `BOARD_RE.md`. (2) Charging: printing while charging was
> **confirmed working on hardware as long as the battery is connected**; the night-2 refusal applied to
> the USB-only, no-battery state. `HARDWARE_TEST_RESULTS.md` carries the correction.
>
> The two sections below are kept as the record of what the documents said before those decisions.

### Recorded before resolution

#### 1. Does the driver clock 383 or 384 head columns? (resolved: 383 loaded, order mirrored)

* `BOARD_RE.md` says "383 clocks per line" and then asserts the loop "does not drop a column".
* `FIRMWARE_MAP.md` lists it as "clocking `height−1` = 383 dots - needs a scope on real hardware".
* `HARDWARE_OBSERVATION.md` pattern 1 concluded "no dropped column" from paper, while pattern 0's note
  computed "a full-width bar = 383 dots per line".
* Our decompilation now shows both `kc02_print_head_ramp` (`0x0205A1FC`) and `kc02_print_run_B`
  (`0x0205A87C`) walk the row starting at `stride × 1` and run 383 iterations, so **bitmap index 0
  is never loaded**. That settles the code question but not the physical one: which head dot receives
  bitmap index 1 depends on the shift order and the head wiring.

**Resolved in favour of the reversing work.** Both drivers load 383 columns from index 1, so bitmap
index 0 is never loaded, and the payloads' `phys_x` mapping puts that unused column at the paper's right
edge (bitmap 0 = reader's right). One 0.125 mm column is not visible by eye, which is why the paper test
could not settle it. What the unused shift-register position actually carries still needs an electrical
check of CLK pulses per LATCH on `PORTE.13`.

#### 2. Is printing refused while charging? (resolved: works with a battery connected)

`HARDWARE_TEST_RESULTS.md` (night 2) states "printing is refused while charging - the message is
'Aufgeladen, nicht verfügbar'. Unplug to print." Today `kc02_battery_check()` returns 14 = charging
on every USB-powered print, and roughly fifteen jobs printed successfully at that status, including
full-width and dense patterns.

**Resolved on hardware:** with the battery connected, printing while charging works, including
full-width and dense patterns, with `kc02_battery_check()` still reporting 14. The night-2 refusal
applied to the USB-only, no-battery state. Status 14 must not be read as "battery absent". The
correction is recorded inline in `HARDWARE_TEST_RESULTS.md`.

#### 3. Firmware identity

Early documents quote `md5 7236c4c1bd3f12b1d85d33a9bebdf510` for `original_firmware_backup.bin`.
The canonical `firmware/original.bin` is `md5 757f58ae6d2c729469fe873e95df794a`,
`sha256 218c6cdf…`. An early dump pair was already known to differ by 8 bytes in one SFAT counter run,
so the two hashes may be two dumps of one camera rather than two firmware revisions. Nothing in the
repo carries the old md5, so this cannot be confirmed from here. Every tool, build script and test
pins the sha256, and all recent work used that image.

## Coverage: findings against documents

| Finding | Documented in |
|---|---|
| Platform is AX3292 / OpenRISC OR1K, no delay slots | `../README.md`, `BOARD_RE.md`, `FIRMWARE_MAP.md` |
| CPU mapping `CPU = file + 0x01FFDA00`; boot section differs | `BOOT_MAP.md`, `FIRMWARE_MAP.md`, `HARDWARE_OBSERVATION.md` |
| Toolchain reconstruction and validation | `../toolchain/`, `TOOLCHAIN_TESTS.md` |
| Naming census (2314 functions, 2234 named, 80 unnamed) | `NEXT_STEPS.md`, `NAMING.md` |
| SDK provenance and similarity alignment | `SDK_MAP.md`, `NEXT_STEPS.md` |
| Typed structs and prototypes from the SDK | `STRUCTS.md`, `report_protos.md` |
| Camera screen: 33 entries, rebuilt module, host tests | `CAMERA_SCREEN.md`, `camera_screen/REBUILD.md` |
| Main menu: 15 entries, rebuilt module, host tests | `MAIN_MENU_SCREEN.md`, `main_menu/REBUILD.md` |
| Printer drives 32 sub-strobes per line, per-dot grayscale | `PRINT_PIPELINE.md` §3b, `BOARD_RE.md`, `USB_FEATURES.md` |
| Threshold mode 0 = ramp, 1 = fixed `0x80` | `PRINT_PIPELINE.md` §3b, `hw/post_repair/README.md` |
| Head dot 0 is the paper's right edge | `HARDWARE_OBSERVATION.md`, `hw/post_repair/README.md` |
| 8 dots/mm across and along, 203 dpi | `HARDWARE_OBSERVATION.md`, `NEXT_STEPS.md`, `hw/print_test/README.md` |
| Dense patterns need 2-dot cells; 1-dot dither saturates | `hw/post_repair/README.md` |
| Print a card JPEG through the firmware VFS | `APP_MAP.md`, `FIRMWARE_MAP.md`, `BOARD_RE.md`, `hw/post_repair/README.md` |
| Decoder needs 3-component YCbCr 4:2:0 | `IMAGE_GEN_RULES.md`, `NEXT_STEPS.md`, `hw/make_carrier.py` |
| FAT directory cache needs a power cycle | `NEXT_STEPS.md`, `CHILD_MODE.md` |
| Payload entry must sit at `CODE_ADDR` (`.entry`) | `NEXT_STEPS.md`, `hw/post_repair/README.md` |
| Region lengths must be evidence-based; scan tool | `HARDWARE_OBSERVATION.md`, `hw/scan_ram_access.py` |
| Battery repair removed the supply ceiling | `hw/post_repair/README.md`, `HARDWARE_OBSERVATION.md` |
| Jam recovery and the diagnostic probe payload | `hw/post_repair/README.md`, `HARDWARE_OBSERVATION.md` |
| Maze, checkerboard, tone and art print artefacts | `hw/post_repair/README.md`, `PRINT_PIPELINE.md` |
| WebUSB and printer-class feasibility | `USB_FEATURES.md` |

## Known gaps still undocumented

* **80 unnamed functions** and the unresolved prototype types. `NEXT_STEPS.md` carries the counts and
  the method, but there is no single list of what is still unknown per subsystem.
* **Shared exports duplicated between screens.** `camera_screen/raw/` and `main_menu/raw/` contain
  seventeen byte-identical dependency exports, and `camera_screen/abi/` duplicates two of its own raw
  files. Harmless snapshots, but a reader cannot tell which copy is authoritative.
* **Stray payload copies.** `hw/sensorprobe.c` and `hw/payloads/sensorprobe.c` are identical, as are
  `hw/post_repair/payload_used.c` and `hw/payloads/ramprint.c`, and
  `hw/post_repair/power_before.json` and `hw/post_repair/power/decoded.json`.
* **No index of print status codes in one place.** They are spread across `BOARD_RE.md`,
  `PRINT_PIPELINE.md` and `hw/post_repair/README.md`.
* **The GUI/Ghidra symbol provenance** for the 2234 named functions lives in
  `tools/ghidra_symbols.tsv`; the docs explain the method but not how to re-apply it selectively.
