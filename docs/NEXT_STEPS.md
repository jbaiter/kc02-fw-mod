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
  id/id_reg/I2C command bytes), `kc02_printer_state` (counter, SFR 0x4800 snapshot, motor phase),
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
