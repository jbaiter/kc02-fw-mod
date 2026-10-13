# KC02 firmware — master map

Index and summary of the complete mapping of `firmware/original.bin` (AppoTech/Buildwin **AX3292**,
little-endian OpenRISC ORBIS32, no delay slots), sha256
`218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5`.

**Address mapping:** `CPU = file_offset + 0x01FFDA00`, `file = CPU − 0x01FFDA00`. Both are given for
every address below. This relation holds for the **app image** (file `0x2600+` ⇒ CPU `0x02000000+`); the
**boot section has a different origin** (see §10).

Detailed reports (each with instruction-level evidence, dense tables):

| Report | Contents |
|---|---|
| `map-ram-data.md` | function census, memory layout, data region, table census, RAM/global map, structures, SFAT |
| `map-screens-ui.md` | task/mode framework, the 5 games, key tables, `ui_state_dispatch`, app context, large tables |
| `map-pipelines.md` | video record/playback, audio, USB, storage, display, power, AVI/FatFs glue |
| `map-sdk-align.md` | vendor linker-map alignment → vendor names for 1280 functions |
| `BOARD_RE.md` / `BOOT_MAP.md` / `SDK_MAP.md` / `APP_MAP.md` / `NEXT_STEPS.md` (in `~/.src/kc02-fw-mod/docs`) | board RE, boot chain, SDK fingerprint map, app notes, open work |

Machine-readable sidecars: `function_census.json`, `ram_globals.json`, `sdk_match_all.json`,
`sdk_align.json`, `sfr_map.tsv`, `sdk_labels.tsv` (all under `/tmp/kc02-board-re/` and copied next to
the reports / into the Ghidra workspace `knowledge/` dir).

## Corrections to earlier notes (read this first)

1. **The five "screen loops" at `0x0207BB9C` are the five GAMES**, not camera/menu/playback/settings:
   Snake, Maze/level-puzzle, Crate/box-puzzle, Tetris, Bouncing-balls. `0x020CCF04` is the **game-select
   index** (0..4), written only by the game-menu handlers `0x020145E8`/`0x0201465C`/`0x02014230`;
   `game_menu_launch_game 0x0201451C` copies the table and launches **mode 8** (`"Game"`), whose service
   `0x020201F8` runs the selected game to completion and returns to mode 7 (`"Game menu"`).
2. **`ui_mode_request(mode,arg)` does not set a screen index.** It stores into the 12-entry **task table**
   at `0x020CCD5C` (and a requested-mode word `0x020CCD50`); `taskService_loop 0x02008188` runs
   close→open→service of the selected task. Modes == tasks (§3).
3. **`0x020D0F10` is not an app context** — it is exactly the start of **SFAT asset #0** (JPEG `FF D8 FF E0`).
   Ghidra's `JPEG_ram_020d0f10._N_M_` names are an artefact of the whole-image import; N is a *file* offset.
   The real, code-referenced **app context is `0x020D416C`** (a 0x78-byte struct) = `0x020D0F10 + 0x325C`
   in the import's linear space (§8).
4. `docs/APP_MAP.md`'s "menu mode task map `0x020C2B50`" is the **mode 3 (`"Photo Encode"` = camera) key
   table**; `0x020C2FB4` is the **mode 4 (`"Play Back"`) key table**. All ten per-mode key tables are listed
   in `map-screens-ui.md` §5 and below (§6).

## 1. Memory layout (measured)

| CPU range | file range | bytes | kind |
|---|---|---|---|
| `0x01FFDA00`–`0x01FFDE00` | `0x0`–`0x400` | 1024 | BLDR header, SFC timing params, SFAT header |
| `0x01FFDE00`–`0x02000000` | `0x400`–`0x2600` | 8704 | boot/exception code (21 candidate entries) — different origin, §10 |
| `0x02000000`–`0x0207B818` | `0x2600`–`0x7DE18` | 505 880 | **application code** — 2609 functions, 98.9 % byte coverage |
| `0x0207B818`–`0x020C6200` | `0x7DE18`–`0xC8800` | 305 640 | app data/rodata: string pools, dispatch tables, mode descriptors, UI strings |
| `0x020C6200`–`0x020F4000` | `0xC8800`–`0xD65FF` | ~185 KiB | **BSS / RAM globals** (app context 0x020D416C, device registry, keypad/UI/ISP/printer state) |
| `0x020D0C00`–`0x020D3510` | `0x0D3200`–`0x0D3510` | 785 | SFAT asset table (8-byte header + 97 × 8-byte entries) |
| `0x020D3510`–`0x02307200` | `0x0D3510`–`0x307200` | ~2.3 MiB | asset data (BMP/JPEG/WAV/raw) |
| `0x02307200`–`0x023FD9FF` | `0x307200`–`0x3FFFFF` | ~1 MiB | erased flash (`0xFF`) |

Notable data sub-regions: string pools at `0x0207B818` (device names, file suffixes `VIDEO/ PHOTO/ AUDIO/`,
`DestBin.bin`, `upgrade failed!!!`, `329X_V1.0.0`, `JRX_20220309`) and `0x02080000`–`0x02097000`
(media/UI strings + the seven large jump tables); `0x02097000`–`0x020C2000` is **~176 KiB of unclassified
`{u16 value; u16 id}` records** with ascending ids — almost certainly a glyph/resource index (open item).

## 2. Function census

| heuristic entries (entry-after-`l.jr r9`) | **2630** = 2609 app + 21 boot |
| confirmed (call/jump target, stored pointer, or Ghidra function) | 2137 |
| source breakdown (heuristic census) | app 1873, unknown 480, sdk 239, board 38 |
| **vendor-map alignment** | 1971 SDK `.text` symbols → 1405 tied to a blob address, **1280 high-confidence**; 1217 mappings match the SDK function's exact size; 20 conflicts recorded (never overwritten) |
| Ghidra project after alignment | **2313 functions, ~1816 named (78.5 %)**, `FUN_*` down from 999 to 497; named functions cover 62.8 % of code bytes |
| code-region byte coverage (heuristic census) | **98.9 %** (gaps are literal pools/alignment; largest 1132 B) |

Function density by 64 KiB: `0x02000000` 329 (boot/device/keypad/UI/photo-print) · `0x02010000` 401 (UI
screens/nav) · `0x02020000` 382 (UI/menu tasks) · `0x02030000` 483 (ISP/CSI/OSD/LCD HAL) · `0x02040000` 357
(ISP/MJPEG/USB/encode) · `0x02050000` 321 (ISP/encode, printer `0x02059000-0x0205AC00`, sensor
`0x02057F00-0x02058600`) · `0x02060000` 253 (FatFs/SD/AVI) · `0x02070000` 83 (thumbnail/rotate/tail helpers).

## 3. The task/mode framework (the app's backbone)

`sysTask { char *name; u32 arg; void (*open)(u32); void (*close)(u32); void (*service)(u32); }` — 20 bytes.
Ten tasks registered at boot (`kc02_task_register_all` @ CPU `0x02000000` → `taskRegister` `0x02008070`
→ `taskArray[id] = *(0x020CCD5C + id*4)`):

| # | name | struct (CPU / file) | open | close | service |
|---:|---|---|---|---|---|
| 1 | `Power Off` | `0x020C3334` / `0x0C5934` | `0x0200C658` | – | – |
| 2 | `Video Recorde` | `0x020C33B0` / `0x0C59B0` | `0x0200DFE8` | `0x0200D860` | `0x0200D620` |
| 3 | `Photo Encode` (camera) | `0x020C2AD4` / `0x0C50D4` | `0x020082F4` | `0x020083C8` | `0x020084A0` |
| 4 | `Play Back` | `0x020C2F90` / `0x0C5590` | `0x0200AFF4` | `0x0200AB04` | `0x0200B06C` |
| 5 | `Audio Player` | `0x020C4EF0` / `0x0C74F0` | `0x020146D4` | `0x02014714` | `0x02014738` |
| 6 | `Main menu` | `0x020C4C18` / `0x0C7218` | `0x020130E4` | `0x020130D0` | `0x020130BC` |
| 7 | `Game menu` | `0x020C4E1C` / `0x0C741C` | `0x02013AA0` | `0x02013A8C` | `0x02013A78` |
| 8 | `Game` | `0x020C64FC` / `0x0C8AFC` | `0x02020530` | `0x0201F020` | `0x020201F8` |
| 9 | `Setting menu` | `0x020C5408` / `0x0C7A08` | `0x02015DB4` | `0x02015DA0` | `0x02015D8C` |
| 10 | `usb device` | `0x020C339C` / `0x0C599C` | `0x0200D498` | `0x0200D450` | `0x0200D588` |

Framework: `taskInit 0x02007FE8` (zeroes the 12-entry array), `taskRegister 0x02008070`,
`ui_mode_request 0x02008108` (`nextTaskId = mode`, `taskArray[mode]->arg = arg`, no-op if
`nextTaskId == 1`), `taskService_loop 0x02008188` (close→open→service; current task ptr at
`0x020CCD58`), getters `0x020080A8`/`0x020080C8`/`0x020080E8` (prev/current/next at `0x020CCD4C/54/50`).
Per-frame `app_main_loop 0x02000404`: `sys_irq_clear 0x0202BC7C` → `system_tick 0x0200D2C0`
(10 ms tasks incl. `kc02_keypad_poll 0x0200CC20`) → `ui_key_dispatch 0x020079B4` → `video_present 0x0205B8C4`.

Boot: `app_boot_init 0x020000BC` (device index lookup) → `app_init 0x0200046C` (register tasks, install boot
key hooks, `ui_mode_request(10)` if USB-connected else `ui_mode_request(6)` — the CHILD_MODE patch site at
CPU `0x020004AC`) → `taskService_loop`.

Menu → mode: `main_menu_ok 0x02013938` switches on `*(0x020D41D3)` (0..5) →
modes 3 (camera), 2 (video), 5 (audio), 4 (playback), 7 (game menu), 9 (settings).

## 4. Games (mode 7 → mode 8)

`game_menu_launch_game 0x0201451C` copies the 5-entry table `0x0207BB9C`, indexes it with `0x020CCF04`
and calls `ui_mode_request(8,0)`. Mode 8's service `0x020201F8` runs `(*(code*)0x020D6A0C)()` and then
`ui_mode_request(7,0)`.

| `0x020CCF04` | game loop (CPU / file) | icon res | asset | game |
|---:|---|---:|---:|---|
| 0 | `0x0201D81C` / `0x01FE1C` | 0x0F | #14 | **Snake** (20×14 cell grid, linked body list) |
| 1 | `0x02020E88` / `0x023488` | 0x0C | #11 | **Maze / level puzzle** (22×15 board, 10 levels × 330 B) |
| 2 | `0x0201DF5C` / `0x02055C` | 0x0D | #12 | **Crate / box puzzle** (8×8 board, 30-px tiles) |
| 3 | `0x0201EEFC` / `0x0214FC` | 0x0E | #13 | **Tetris** (12×16 bit grid, rotate/gravity/clear) |
| 4 | `0x0201CA24` / `0x01F024` | 0x0B | #10 | **Bouncing balls** (sprite engine + modal dialog) |

All five share the pattern `init(); for(;;){ XMsgQPend(*(0x020D41E0), &st); switch(key){…} tick(); }`.
Game-menu carousel: `game_menu_next/prev 0x020145E8`/`0x0201465C`, animation `0x02013B0C`, draw `0x02013F54`,
thumbnails from `0x0207BBB0` in `0x020CCEE8[5]` (120×120).

## 5. Names the firmware gives itself (string table)

File `0x7DE00`–`0x7E400` (CPU `0x0207B800`–`0x0207BE00`): `"Main menu"`, `"Game menu"`, `"Photo Encode"`,
`"Video Recorde"`, `"Play Back"`, `"Audio Player"`, `"Music List"`, `"Setting menu"`, `"Power Off"`,
`"usb device"`, `"Game"`; `"329X_V1.0.0"`, `"JRX_20220309"`, `"2025/12/12 V1.3"`; `"chip:"`, `"AX3292"`;
`"DestBin.bin"`, `"SELFTEST.bin"`, `"exmend.bin"`, `"upgrade failed!!!"`; `"VIDEO/"`, `"PHOTO/"`, `"AUDIO/"`;
device names `battery`, `usb`, `ad-key`, `sd-card`, `led`, `lcd`, `ir`, `cmos-sensor`; SELFTEST text
(`"usensor:"`, `"offline"`, `"Gsensor:"`, `"tuning:"`, `"battery:"`, `"keyADC value:"`).

`JRX` is also the magic validated in `kc02_print_file`'s 12-byte record header — the app's own file format,
build date 2022-03-09.

## 6. Key handling

* **Hook tables**: primary `0x020CCC8C` and secondary `0x020CCBD4` (46 entries each), filled by
  `key_hooks_register_primary 0x02007880` (vendor name `msgFuncRegister`, `msgDeal.c`) and
  `key_hooks_register_secondary 0x02007938` (`unshieldedMsgFuncRegister`) from the tables
  `0x020C2A44` (built-in), `0x020C2A64` (boot), and each mode's key table.
* **Event word**: `key_id | 0x10000` (auto-repeat) `| 0x20000` (release). Dispatcher `ui_key_dispatch
  0x020079B4` calls `hook(ui_ctx, 1, &flags)`; `ui_ctx = *(0x020CE0D8)` (`ui_get_ctx 0x0202A1B0`).
* **Per-mode key tables** (all verified): camera (mode 3) `0x020C2B50`; playback (4) `0x020C2FB4`;
  audio (5) `0x020C4F14`; main menu (6) `0x020C4C3C` (11 entries); game menu (7) `0x020C4E40`;
  game (8) `0x020C6520` (stubs only — the game loop consumes its own events); settings (9) `0x020C542C`;
  video (2) `0x020C33F4`; USB (10) `0x020C3354`. Message-id vocabulary is shared across modes
  (`0x1A`/`0x23` OK, `0x1B`-`0x1E` d-pad, `0x01`-`0x11` system, `0x21`-`0x28` secondary/POWER).
* Also present: descriptor-shaped `{keymap, widget, 0, 0}` structs referenced only from data
  (e.g. `0x020C433C` → keymap `0x020C434C`), invisible to a normal xref pass.
* Boot hooks worth noting: `0x26` (long POWER) → `ui_mode_request(1)` = Power Off; `0x01` → possibly launch
  `SELFTEST.bin`; `0x02` → `ui_mode_request(10,1)` when USB state == 2; `0x0A` → idle countdown → Power Off.

## 7. Tables (complete census)

`ui_state_dispatch` `0x0207B908` (35) · init callbacks `0x0207BA00` (10) · menu icon coords `0x0207BB60` (6)
· menu icon assets `0x0207BB78` (6: 34,37,31,35,33,36) · game callbacks `0x0207BB9C` (5) · per-key jump
tables `0x0207BCEC` (10, crate) and `0x0207BEC8` (12, maze) · mode descriptors (10 × `sysTask`) · per-mode
key tables (9) · large tables `0x0208044C` (148, sUI window proc jump table), `0x0208071C` (90, sUI list/scroll),
`0x02080904` (19, clock/pixel SPR switch), `0x020809B0` (25, ISP model→mask), `0x02081EE4` (191, bank of ISP/JPEG
jump tables), `0x02082300` (18, LCD/flash), `0x020836CC` (11) + `0x020838CC` (19) (**genuine USB command
state-machine dispatch**), `0x02083AE0` (17, WAV/RIFF parser), `0x020849A4` (12, JPEG decode states),
`0x02086A20` (27, **audio dispatch**, 9 groups of 3), `0x020970C4` (9, unicode remap), `0x020C2390` (49,
fixed-point/scale helper) · device list `0x020C259C` (9 × `kc02_device_node`) · ADC keypad `0x020C268C` (6) ·
stream/FIFO ops vtable `0x020C26BC` (18) · task array `0x020CCD5C` (12).

## 8. RAM / globals

* **App context `0x020D416C`** (0x78 bytes; = import address `0x020D0F10 + 0x325C`): device indexes
  (`+0x00` lcd `0x020D417F`, `+0x04` battery, `+0x0C` sd-card, `+0x18` usb, `+0x1C` led, `+0x20` lcd?, `+0x24` ir,
  `+0x28` ad-key — exact mapping in `map-screens-ui.md` §7), boot/USB state `+0x02` (`0x020D416E`),
  main-menu item `+0x67` (`0x020D41D3`), camera sub-mode `+0x59` (`0x020D41C5`), scene/effect `+0x58`,
  current file index `+0x30` (`0x020D419C`), photo/list count `+0x34`, playback mode `+0x38`, and the
  pending-mode pair `0x020D41D4/5` used by `kc02_power_handler` (CHILD_MODE's camera-state bytes).
* **UI queue handle `0x020D41E0`** (`app_context + 0x32D0`); created with `XMsgQCreate(&0x020C86A4, 0x14)`.
* **1100 distinct RAM globals** referenced by code (1065 in BSS `0x020C6200`–`0x02100000`);
  `ram_globals.json` lists each with refs, referencing functions and a module vote. Top-referenced ones are
  the keypad/printer/ISP/UI state blocks documented in `BOARD_RE.md`.
* Per-block detail: keypad `0x020C8780`–`0x020C87D8`, `0x020C2598`; printer counter
  `0x020D2EC8` (4 bytes; the words up to `0x020D2EE4` are separate globals) + SFR `0x4800`
  snapshot; stream/FIFO records `0x020C8A3C` (stride 0x4040); sensor ops
  `0x020E32A0`; AVI record contexts `0x020E3180` (channel A) and `0x020DB0F8` (channel B).

## 9. Pipelines (summary; details in `map-pipelines.md`)

* **Video record** (task 2): `task_video_record_open 0x0200DFE8` builds AVI config from `configValue` reads,
  `aviOpen 0x02054C94`, sensor 640×480, installs key table `0x020C33F4`; OK → `vr_start_record 0x0200D72C`
  → `record_stream_start_A 0x02055360` (AVI encode start → MJPEG 640×480 @fps → AUADC audio) or `_B` (second
  channel, `0x020554D0`, ctx `0x020DB0F8`); service `0x0200D620` → **`aviMuxService 0x02055A4C`** pulls MJPEG
  frames (`FUN_020416CC`) + PCM blocks (`hal_auadcBufferGet 0x0203F0BC`), tags them (`aviEncodeFrameTag`
  SDK) and writes via FatFs `write 0x0206C770`; stop `0x0200D7D0` → `record_stream_stop_A/B` + watermark
  close; rollover `0x020558A4/0x020558FC`.
* **AVI/FatFs glue** is SDK code byte-matched (`aviEncoder/encoder_std/encoder_odml/aviDecoder/decoder_std`
  and `libfs.a` `fs.o`: `open 0x0206C5B8`, `close 0x0206C680`, `read 0x0206C6F8`, `write 0x0206C770`,
  `lseek 0x0206C7E8`, `fs_size 0x0206CAF8`).
* **Playback** (task 4) and **audio** (task 5), **USB** (task 10, device modes MSC/UAC/UVC + host
  enumeration `husb_*`), **storage** (FatFs callers, `VIDEO/ PHOTO/ AUDIO/` folders, SD driver
  `ax32xx_sd.o`), **display** (st3030b panel, `lcd_ax32xx_lcd`, OSD overlays), **power/idle** (Power Off
  task, idle countdown `0x02007DA8`, battery monitor, charger gate `0x020CCDC8`, print-block latch
  `0x020C8794`) — all mapped in `map-pipelines.md` with entry points and state addresses.
* **Photo/print** (already documented in `BOARD_RE.md`): capture → `0x020087E8` watermark-aware JPEG encoder
  (quality tier from width, 512 KiB buffer) → `kc02_print_image 0x0205B0FC` / `kc02_print_file 0x0205ADF0`
  (task 3 `0x1A`/`0x23` and mode-4 `0x1A` respectively) → `kc02_print_head_ramp 0x0205A1FC` (GPIO bit-banged
  head: DATA `PORTE.15`, CLK `PORTE.13`, LATCH `PORTB.5`, STROBE `PORTA.9`).

## 10. Boot chain (details in `BOOT_MAP.md`)

`BLDR` header @file `0x0` (magic @4, version `0x00010500` @8, checksum byte @`0x0C`,
**app load address `0x02000000` @`0x20`**, validity markers `0x01234567` @`0x30/0x34`, `0x55AA` @`0x1FE`);
SFC timing params `0x200`; SFAT header `0x300`; boot/exception code `0x400`–`0x25FF` (SRAM origin — needs a
separate base-0 Ghidra import; contains the register-saving exception entry and the flash→SDRAM copy loop at
boot-VMA `0x12AC`); app image `0x2600+`.

## 11. SDK vs app split

**SDK code (reusable platform, byte/shape matched — see `SDK_MAP.md`)**: HAL (`ax32xx_*`, `src_*`), ISP/CSI/
OSD/MJPEG/rotate, FatFs/SD/AVI, USB device+host, RTOS (`xos`: `XMsgQ*`, `XMbox*`, `XWork*`), sUI widgets,
font/icon/string resource helpers, watermark.

**KC02 app code (board/product specific)**: the task/mode framework and the ten tasks, all UI screens and
navigation, the five games, photo/print orchestration, the sensor bring-up (custom resource-item model),
and the printer mechanism (no SDK counterpart). ~480 functions still have no source attribution.

## 12. Ghidra project state (knowledge is embedded)

The shared project (`pi-ghidra` in the SHA-keyed pi-ghidra cache) carries:

* language `KC02:LE:32:nodelay` (LE, no delay slots, `r9 = inst_next`), `spr` space with a 64 KiB block,
  **1089 SFR symbols** from `ax329x.h`, and a `<volatile>` range in `OpenRISC.pspec` so peripheral
  read-modify-writes survive decompilation;
* **structs** `kc02_sys_task` (name/arg/open/close/service) applied to all ten task descriptors and the
  `task_array` at `0x020CCD5C`, plus `kc02_device_node`, `kc02_adc_key_entry`, `kc02_task_map_entry`,
  `kc02_sensor_item`, `kc02_printer_state`, `kc02_sensor_ops_blob`, with typed data applied to the key
  tables;
* **plate comments** with port-bit semantics, game identities, the task framework, and the `0x020D0F10`
  alias warning; **bookmarks** (`KC02`) `overview`/`app-framework`/`games`/`printer`/`keypad`/`sensor`/
  `boot`/`pipelines`/`tooling`/`open-items`;
* **~1816 named functions** (vendor names from the SDK-map alignment where a function was `FUN_*`, our
  descriptive board names kept where they add meaning);
* the full doc set + JSON sidecars copied to `<artifact>/knowledge/`.

Extension recipes (entry-after-`l.jr r9`, `sdk_match.py` shape matching, vendor-map alignment, disassembly
commands) are in the `tooling` bookmark and `docs/NEXT_STEPS.md`.

## 13. Open items

1. **~~176 KiB unclassified data block~~ `0x02097000`–`0x020C2000` — RESOLVED: it is ordinary `.rodata`.**
   It holds the string literals of the image-encode/FatFs/MJPEG/thumbnail modules (`fs : unknow`,
   `>>>>WARNING : fs error .%d,%d`, `MSDOS5.0`, `NO NAME    FAT32   `, `HAL_CFG_MJPEG_WIDTH/HEIGHT`,
   `mjbuffer`, `write yuv file`, `thumbOffset=0x%x`, …) plus **764 monotonic `{u16 id, u16 target}` mapping
   sub-tables (~13.5 k records)** whose targets land in the CJK Unicode range (`0x4D00`–`0x5A00`) —
   i.e. **charset/codepage or glyph-index mapping data**. The SDK's own `libfs.a(ffunicode.o)` `.rodata`
   (59 844 B of codepage tables) does **not** byte-match ours, so this is KC02-specific content of the same
   kind rather than a copy of the SDK tables. The reason a `movhi/ori` scan found no reference is that these
   tables are reached **through pointers** — 32 pointer sites were found from code and data.
2. **480 functions with no source attribution** — refine with the now-complete SDK object set (the toolchain
   lane) and by matching against the SDK demo app's own modules.
3. ~~Boot section link origin~~ — **resolved** (`BOOT_MAP.md`): VMA = file − 0x1FC, all 60 vendor boot symbols
   mapped (`_start`, `_except`, `sdram_init`, `sysClkSet`, DLL/SDRAM tuning, `exception_uart_puts`, …).
4. **Printer details**: `SFR 0x4800` = **`SPR_PICMR`** (interrupt-controller mask) — the print job brackets itself
   with a 13-bit mask save/restore (`kc02_sfr4800_save_restore` snapshots bits 14,0,6,4,5,7,10,13,15,1,2,3,8
   at `0x020D2ECD`–`0x020D2ED8`, testing bit `0x4000`); **`JRX`** = the vendor's tag (JinRiXin), used both as
   the build string `JRX_20220309` and as the 12-byte print-file record magic (writer `0x02056510`,
   validator in `kc02_print_file` at `0x0205AF8C`, comparing bytes `74/82/88`). **Resolved:** the 383-vs-384
   dot-row question. Both print drivers start the row loop at `stride x 1` and load 383 columns, so
   **bitmap index 0 is never loaded** (`kc02_print_head_ramp` `0x0205A1FC`, `kc02_print_run_B` `0x0205A87C`);
   `print_run_B` still enforces `height == 384`. What the unused shift-register position carries is not
   established, and needs an electrical check rather than paper - see the BOARD_RE addendum.
5. `0x020D2EC8` (printer state counter) has no reader in the image; the `ui_state_dispatch` entries 6+ are
   reached via `FUN_02006C74`'s case bodies rather than the table, which is worth double-checking.
