# KC02 main menu: decompilation and controller reconstruction

Firmware: `firmware/original.bin`, SHA-256
`218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5`.
CPU/file mapping: `file_offset = CPU - 0x01FFDA00`.
This work used static image analysis and read-only Ghidra exports, without camera access.

## Files

- [`main_menu/main_menu_screen_decompiled.c`](main_menu/main_menu_screen_decompiled.c):
  combined raw output for **54 functions**, 2,030 lines.
- [`main_menu/raw/`](main_menu/raw/): individual exports, function index, direct
  calls and disassembly. Includes **all 15 menu-region entries** at
  `0x020130BC..0x02013A78` and selected framework/display/resource dependencies.
  It does not include their complete transitive call graph.
- [`main_menu/main_menu_screen_model.c`](main_menu/main_menu_screen_model.c),
  [`main_menu_screen.h`](main_menu/main_menu_screen.h): readable, target-linked C
  reconstruction of all 15 menu bodies with explicit callback signatures.
- [`main_menu/tables.json`](main_menu/tables.json),
  [`widgets.tsv`](main_menu/widgets.tsv), [`keymap.tsv`](main_menu/keymap.tsv),
  [`items.tsv`](main_menu/items.tsv): decoded tables and original byte records.
- [`main_menu/assets/`](main_menu/assets/): background JPEG and six selection BMPs,
  extracted from the matching image with resource IDs, offsets and hashes in `tables.json`.
- [`main_menu/extract_main_menu_tables.py`](main_menu/extract_main_menu_tables.py):
  reproducible, hash-pinned table/asset extraction.
- [`main_menu/REBUILD.md`](main_menu/REBUILD.md): build/test commands and limits.
  The module links as a **2,800-byte** little-endian, no-delay OpenRISC binary.
  Host tests pass **1,101 assertions** with drivers mocked. No device execution.

## Lifecycle and dispatch

Mode 6 descriptor: **`0x020C4C18`**. Lifecycle slots:

| Slot | Callback | Behavior |
|---|---|---|
| `0x020C4C20` | enter `0x020130E4` | disable CSI, apply UI state 7, open menu window |
| `0x020C4C24` | exit `0x020130D0` | no-op |
| `0x020C4C28` | tick `0x020130BC` | no-op |

The window descriptor begins at **`0x020C4C2C`**, within the mode record:
keymap `0x020C4C3C`, widgets `0x020C4C9C`, runtime handle at `0x020C4C34`,
repeat-open flag zero. Stock `win_open` installs its map and sends the window-enter
notification. Window-exit message `0x0F`, not the no-op mode exit, frees the image buffers.

`msgFuncRegister` (`0x02007880`) clears the primary vector at `0x020CCC8C`, loads
built-ins, then installs the supplied window handlers. `ui_key_dispatch`
(`0x020079B4`) invokes primary and optional secondary handlers with
`(current_window, 1, &flags)`, where flags are the queued high word shifted down.
`ui_get_ctx` (`0x0202A1B0`) reads the current-window global `0x020CE0D8`.
The primary vector is not a system-only table.

## Rendering: baked background plus blinking selection

The normal six icons are **part of the 320x240 background JPEG**, resource
**`0x20`**. The C code does not create six icon widgets. It decodes that JPEG
into a retained Y/UV buffer and allocates six **96x96 selection tiles** from BMP resources.
For each repaint it obtains an idle display frame, configures its size, copies
the background and optionally overlays the selected tile with `blit_b`
(`0x02022060`). It flushes the cache and submits the frame through `dispLayerShow`.

Phase **0** overlays the selected tile; a nonzero phase shows the background alone.
The text captions occupy a separate `sUI` overlay. Window enter enables six captions;
OK and message `0x1F` hide them before switching mode.

The blink callback is **message `0x09`**, body `0x02013698`. It toggles the phase
when `last_timestamp + 300 <= XOSTimeGet()`, then refreshes the timestamp.
However, `timerMsgSend` (`0x0200C698`) normally posts message `0x09` only when
**more than 500 XOS ticks** have elapsed. Thus the 300-tick guard does not establish
a 300-ms blink cadence. Queue/loop timing also matters; no wall-clock measurement
was made. Message `0x0A` is a no-op on this screen.

`nv_open`/`nv_size` access `SFAT_BASE + resource_id * 8`, with SFAT at file
`0x0D3200`. Its eight-byte header means **NV resource IDs are one-based** relative
to the asset extractor's zero-based entries: background ID `0x20` is SFAT entry 31.
Using zero-based entry 32 selects a BMP instead of the background JPEG.

## Menu items and keys

The coordinate table at **`0x0207BB60`** and icon-resource table at
**`0x0207BB78`** describe a three-column, two-row grid:

| Index | Action from OK routing | Mode | Tile x,y | NV tile resource | Caption resource |
|---|---|---|---|---|---|
| 0 | Camera | 3 | 12,18 | `0x22` | `0x81000042` |
| 1 | Video recording | 2 | 112,18 | `0x25` | `0x81000041` |
| 2 | Audio player | 5 | 212,18 | `0x1F` | `0x81000044` |
| 3 | Playback | 4 | 12,126 | `0x23` | `0x81000043` |
| 4 | Game menu | 7 | 112,126 | `0x21` | `0x81000045` |
| 5 | Settings | 9 | 212,126 | `0x24` | `0x81000040` |

Action names follow the target mode descriptors; literal caption text remains in
stock localized string resources. The window table contains a **320x240 root and
six string widgets** with **48-byte records**, followed by a terminator at `0x020C4DEC`.
Caption IDs are 0..5; each is 98x25. Top-row captions start at y=106 and bottom-row
captions at y=214. Caption x positions are 12,112,214, slightly different from the last icon column.
The opaque eight-byte record extensions remain unchanged.

| Message | Raw handler | Behavior under event=1, flags=0 |
|---|---|---|
| RIGHT `0x1E` | `0x02013714` | index +1, wrap 5 to 0 |
| LEFT `0x1D` | `0x0201379C` | index -1, wrap 0 to 5 |
| DOWN `0x1C` | `0x02013828` | swap rows in the same column: +3 or -3 |
| UP `0x1B` | `0x020138B0` | same row swap as DOWN |
| OK `0x1A`, alternate OK `0x23` | `0x02013938` | hide captions and request selected mode |
| `0x1F` | `0x02013A14` | hide captions and request playback directly |

Navigation resets blink phase to zero, redraws selection and records the current
time. Enter clamps an out-of-range saved selection to zero; otherwise it preserves
that selection. Audio selection has an extra transition: if context `+0x68` is
zero, it sets `+0x68=1` and `+0x69=0` after requesting mode 5. Other selections do not.
The exact physical key corresponding to message `0x1F` has not been established here.

## Shared state, allocation and ABI cautions

| CPU address | Use in menu code |
|---|---|
| `0x020D41D3` | selected index, application context +`0x67` |
| `0x020CCEAC`, `0x020CCEAE` | tile width/height, both 96 |
| `0x020CCEB0..0x020CCEC7` | six decoded-tile pointers |
| `0x020CCEC8`, `0x020CCECA` | background height/width, in that order |
| `0x020CCECC` | decoded-background pointer |
| `0x020CCED0` | blink phase byte |
| `0x020CCED4` | blink timestamp word |

At 320x240, explicit retained image allocations total **198,144 bytes**:
115,200 background bytes plus six 13,824-byte tiles. Initialization adds a
27,648-byte BMP scratch buffer, for **225,792 bytes peak**, excluding stock
framebuffers/allocator overhead. Normal initialization frees that scratch buffer;
window exit frees the background and six tiles.

The stock initializer returns zero even on allocation failure. A failed tile
allocation leaks the scratch buffer. The reconstruction preserves that behavior
and tests it, rather than presenting a repaired implementation as decompilation.
Decode return values and dimension errors also lack recovery in these bodies.

Raw output contains **15 warning comments** and misleading prototypes:

- Key handlers read event from **r4** and the flag pointer from **r5**, even when
  Ghidra emits a two-argument prototype with an unused leading argument removed.
- `dispLayerShow` receives **r3=0, r4=frame**; the raw renderer shifts these and
  appears to pass the frame as its first argument.
- The JPEG resource helper receives **five arguments**, including dimensions in r6/r7.
- The BMP helper receives **six arguments**; the blitter receives **12**, including
  six stack arguments. Menu call sites preserve a height/width ordering that only
  becomes obvious for non-square tiles. Audit it before changing dimensions.
- Inherited `winDestroy`/`ui_close_all_windows` labels identify paint routines;
  see [CAMERA_SCREEN.md](CAMERA_SCREEN.md), not a destructor API inferred from names.

The exporter used `-readOnly -noanalysis` under `/tmp/kc02-board-re/ghidra.lock`.
Four missing function definitions existed only in the transient export and were
discarded. The shared project, firmware image and device state remain unchanged.
Rebuilding and host tests do not validate load placement, the full target ABI,
hardware drawing or persistent firmware integration.
