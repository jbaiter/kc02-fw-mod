> **CORRECTIONS — superseded by `docs/FIRMWARE_MAP.md` and `map-screens-ui.md`.**
>
> 1. The five callbacks at `0x0207BB9C` are **the five games** (Snake, Maze, Crate, Tetris, Bouncing balls),
>    not camera/menu/playback/games/settings. `0x020CCF04` is the **game-select index**, not a screen index.
> 2. Modes are **tasks**: the ten `sysTask` descriptors and the task array `0x020CCD5C` (see `FIRMWARE_MAP.md` §3).
>    "Menu mode" key table `0x020C2B50` is really **mode 3 = `"Photo Encode"` (camera)**; `0x020C2FB4` is
>    **mode 4 = `"Play Back"`**.
> 3. `0x020D0F10` is **not an app context** — it is the start of **SFAT asset #0**; the real app context is
>    **`0x020D416C`** (0x78 bytes) = `0x020D0F10 + 0x325C` in the import's linear space.

# KC02 application layer map

The app on top of the AX3292 platform: a table-driven state machine with five screen event loops,
per-task message maps, per-key switch jump tables, and a photo/capture pipeline. Built from the
firmware blob (`firmware/original.bin`, sha256 `218c6cdf…21a5`) with `app.dis` / `full.dis`, the Ghidra
project (language `KC02:LE:32:nodelay`), and the SDK function map (`docs/SDK_MAP.md`).

Address convention: `CPU = file_offset + 0x01FFDA00`.

## Shape of the app

```
main loop (CPU 0x02000404)
  ├─ system tick            0x0200D2C0   (10 ms gated tasks, e.g. the ad-key poll at 0x0200CC20)
  ├─ UI key dispatcher      0x020079B4   drains the message queue, calls key hooks
  └─ current screen loop    via the mode-callback table
        screen index byte   0x020CCF04   (index * 4 into the 5-entry table at 0x0207BB9C)
```

* **App context / globals** live at CPU `0x020CCxxx`–`0x020Dxxxx` (RAM). In a whole-image import these
  addresses overlap flash *asset* bytes, which is why Ghidra emits names like `JPEG_ram_020d0f10._NNNN`
  for app variables — ignore the `JPEG_` prefix, it is an artifact of the overlapping import.
* **Mode requests**: `ui_mode_request` @ `0x02008108`, 28 call sites. Observed modes and call counts:
  **mode 6 (main menu) ×9**, **mode 1 ×9**, mode 7 ×2, and 2, 4, 5, 8, 9, 10, 11 once each.
  `CHILD_MODE.md` documents mode 6 = main menu and mode 3 = camera (the value the child-mode boot patch
  sets); modes are the *logical* screens, the 5-entry callback table is the *implemented* set.

## Screen event loops (mode callbacks, table at CPU `0x0207BB9C`)

| idx | entry | character (from decompilation) |
|---|---|---|
| 0 | `0x0201D81C` | allocates `0x12C00` = **76800 B = one 320×240 framebuffer**; pumps the UI queue; switches on keys `0x1B/0x1C/0x1D/0x1E` (UP/DOWN/LEFT/RIGHT), `0x0B`, `0x24/0x26` (POWER/long-POWER). Camera/photo screen |
| 1 | `0x02020E88` | navigation loop: keys `0x1B..0x1E` map to `(dy,dx) ∈ {-1,0,1}` pairs, `0x26` → `ui_mode_request(1,0)`, `0x24` → exit. Grid/menu browsing |
| 2 | `0x0201DF5C` | navigation loop (`FUN_0201d914`, `FUN_0201de00`, `FUN_0201da70` = draw/advance/confirm), same key set |
| 3 | `0x0201EEFC` | navigation loop; posts `0x24` back to the queue when `0x020CD678 != 0` (power/standby path) |
| 4 | `0x0201CA24` | fifth screen loop |

Each loop is `XMsgQPend(*(ctx+116), &st) → switch(key)`; the per-key bodies are the `caseD_<key>`
blocks that the table annotator found (`caseD_1b … caseD_24`), i.e. **key handling is compiled as jump
tables inside each screen**.

## UI state dispatch (file `0x07DF08`, CPU `0x0207B908`) — 35 entries

Named `ui_state_dispatch_00…34` in the Ghidra project. Entries 0–5 are the main-menu handlers
(per the earlier theme work: camera, video, music/MP3, playback, games, settings); 16 entries point at
the shared null handler `0x02006DD4` (`ui_state_dispatch_07`); the rest are secondary UI states
(`…_06`, `…_12`–`…_14`, `…_17`, `…_19`–`…_22`, `…_27`, `…_32`–`…_34`).

Related tables (all in CPU `0x0207B9xx`–`0x0207BFxx`):

| file | CPU | size | role |
|---|---|---|---|
| `0x07E000` | `0x0207BA00` | 10 × 4 | init callbacks (`0x020093AC…0x020093F4`) |
| `0x07E160` | `0x0207BB60` | 6 × (u16,u16) | menu icon coordinates (12,18),(112,18),(212,18),(12,126),(112,126),(212,126) |
| `0x07E178` | `0x0207BB78` | 6 × u32 | menu asset indices 34,37,31,35,33,36 |
| `0x07E19C` | `0x0207BB9C` | 5 × u32 | screen callbacks (indexed by `0x020CCF04`) |
| `0x07E2EC` / `0x07E4C8` | `0x0207BCEC` / `0x0207BEC8` | 10 / 12 × u32 | per-key jump tables (`caseD_1b…caseD_26`) |

## Task / message maps (msg → handler)

Each task has a `{u32 msg, u32 handler}` map; the app posts message ids to a queue and the task table
resolves the handler. Four maps found:

| CPU | file | entries | identity |
|---|---|---|---|
| `0x020C2A44` | `0x0C5044` | 3 | msg `0x00`→`0x02007830`, `0x02`→`0x02007BD4`, `0x03`→`0x02007BEC` |
| `0x020C2A64` | `0x0C5064` | 13 | **boot table** (installed from `0x020004C0`): `0x1B`→`0x02007D30`, `0x1C`→`0x02007CF4`, `0x24`→`0x02007C04`, `0x26`→`0x02007E20`, `0x01`→`0x02007F7C`, `0x02`→`0x02007EE4`, `0x03`→`0x02007E5C`, `0x0A`→`0x02007DA8`, … |
| `0x020C2B50` | `0x0C5150` | 15 | **menu mode** (referenced by the mode descriptor `0x020C2B40`): `0x0E`→`0x0200A37C`, `0x0F`→`0x0200947C`, `0x10`→`0x0200A310`, `0x1B`→`0x02009890` (UP), `0x1C`→`0x02009568`, `0x1D`→`0x02009DE0`, `0x1E`→`0x02009F6C`, `0x1A`/`0x23`→`0x0200A4BC` (OK → **print image**), `0x24`→`0x02009F08` (POWER), `0x01`→`0x02009E70`, `0x02`→`0x02008D74`, `0x03`→`0x02008D3C`, `0x06`→`0x02008BD4`, `0x0A`→`0x02009108` |
| `0x020C2FB4` | `0x0C55B4` | 15 | second mode's task map: `0x1A`→`0x0200C24C` (**print file**), `0x0E`→`0x0200BE58`, `0x0F`→`0x0200B8CC`, `0x10`→`0x0200BDB0`, `0x24`→`0x0200BD4C`, `0x1B`→`0x0200C12C`, `0x1D`→`0x0200BC88`, `0x1E`→`0x0200BC00`, `0x01`→`0x0200BB68`, `0x02`→`0x0200B468`, `0x03`→`0x0200B128`, `0x06`→`0x0200B140`, `0x0A`→`0x0200C08C`, `0x0D`→`0x0200B158` |

Message ids are shared across tasks (e.g. `0x1A` = OK, `0x24` = POWER, `0x01`/`0x02`/`0x03`/`0x06`/`0x0A`
= common navigation/actions) — which is why the per-mode tables differ mainly in the *handler*
addresses, not the key list.

## Screen / task names (the firmware names them itself)

The app carries a string table at file `0x7DE00`–`0x7E400` (CPU `0x0207B800`–`0x0207BE00`) that names its
screens, tasks, folders, version/build tags and update files. Extracted verbatim:

| CPU | string | role |
|---|---|---|
| `0x0207B81C` | `"battery"`, `"ad-key"` `0x0207B828`, `"sd-card"`, `"cmos-sensor"` | device names (duplicated from the descriptor region) |
| `0x0207B99F` | `"329X_V1.0.0"` | firmware version string (also seen in the dump early on) |
| `0x0207B9D8` | `"JRX_20220309"` | **build tag** — `JRX` is the same magic as the 12-byte record header `kc02_print_file` validates, and 2022-03-09 the build date |
| `0x0207BB43` | `"2025/12/12 V1.3"` | second version/date string (SELFTEST/version display) |
| `0x0207BB36` | `"chip:"`, `"AX3292"` `0x0207BB3C`, `" null"`, `"NGame"` `0x0207BEBF` | SELFTEST chip/diagnostic labels |
| `0x0207BA60` | `"Play Back"` | playback screen/task |
| `0x0207BA72` | `"play back"` | (lower-case variant) |
| `0x0207BA7C` | `"Power Off"` | shutdown task |
| `0x0207BA8B` | `"usb device"` | USB mass-storage mode task |
| `0x0207BA96` | `"Video Recorde"` | video record task |
| `0x0207B9F2` | `"Photo Encode"` | still capture / encode task |
| `0x0207BB53` | `"Main menu"` | main menu screen |
| `0x0207BB90` | `"Game menu"` | games screen |
| `0x0207BBC4` | `"Audio Player"` | music player screen |
| `0x0207BBD9` | `"Music List"` | music list screen |
| `0x0207BBE4` | `"Setting menu"` | settings screen |
| `0x0207BACC` | `"usensor:"`, `"offline"`, `"usensor work:"`, `"show off"`, `"usensor eeprom:"` | SELFTEST/diagnostic text |
| `0x0207BB08` | `"Gsensor:"`, `"tuning:"`, `"battery:"` | SELFTEST/diagnostic text |
| `0x0207B852` | `"VIDEO/"`, `"PHOTO/"`, `"AUDIO/"` | on-card folder layout |
| `0x0207B887` | `"DestBin.bin"`, `"SELFTEST.bin"` `0x0207B9E5`, `"exmend.bin"` `0x0207B994`, `"upgrade failed!!!"` `0x0207B893` | update filenames + failure banner |

This is the authoritative list for naming the five screen loops and the task handlers, and it confirms the
menu map (camera/photo, video, audio, playback, games, settings).

## Photo / capture pipeline

| Stage | Function | Notes |
|---|---|---|
| sensor bring-up | `sensor_init` `0x020583F0` | LDO → MCLK 16 MHz → IIC1 200 kHz → `sensor_autocheck` (probe reg `0x0B` for `0x48`) → MCLK 24 MHz → init table (100 pairs) |
| CSI hand-off | `csi_register_sensor` `0x020381C8` | stores `{item, ops}` at `0x020D7F38` |
| CSI task | `0x02046500` | reads `ops = *(0x020D7F38+0x10)`, validity-tests `ops[+0xC70]` (via the tiny sign test `0x02032844`) and calls it with the two capture buffers → **sensor exposure/gain callback** |
| still capture → JPEG | `0x020087E8` | closes/reopens the **watermark** (`hal_watermarkClose`), reads config `0x0E`/`0x18`, gets sensor `w,h` (`0x020381FC`), picks a quality tier from width (`>0x500→3`, `>0xA00→4`, `>0xC80→6`), starts the encoder (`0x02040C6C(w,h,q,flip,0)`), allocates a **512 KiB** output buffer, pulls encoded chunks (`0x020416CC(0,…)`) with a **2 s timeout** — this is the photo-save *and* the print source |
| JPEG decode (print) | `kc02_load_print_image` `0x020593CC` | YUV420 decode + rescale to the 384-dot head canvas |
| print | `kc02_print_image` `0x0205B0FC` / `kc02_print_file` `0x0205ADF0` | see `docs/BOARD_RE.md` |

The UI tasks that drive printing are in the menu-mode task map: msg `0x1A`/`0x23` → `0x0200A4BC`
(print in-RAM image; calls the encoder above and then `kc02_print_image`), and in the second map
msg `0x1A` → `0x0200C24C` (print file; validates a 12-byte `"JRX"` record header, then `kc02_print_file`).

## SDK-mapped UI/ISP surface (from `docs/SDK_MAP.md`)

The app reuses the SDK's UI framework and resource helpers, which is why these matched by fingerprint:
`sUI` widgets/menu items/winManage (19 functions), `res_*` font/icon/string helpers, `std_*` helpers,
`isp_hal_isp` (11), `src_ax32xx_isp` (12), `osd_ax32xx_osd` (10), `mjpeg_ax32xx_mjpeg` (7),
`hal_watermark` (2), `src_hal_stream` (5), `src_hal_mjpeg(B)` (8) — i.e. **the drawing, OSD, JPEG codec
and watermark layers are SDK code**, while the screen loops, task maps and photo/print orchestration are
KC02 app code.

## Open items

1. Identify screens 1–4 precisely (the navigation loops): resolve the draw/confirm helpers
   (`0x0201D914`, `0x0201DE00`, `0x0201DA70`, `0x02020DEC`, `0x0201ECA0`, …) and the resource ids they
   draw, then name the callbacks (`screen_camera`, `screen_menu`, `screen_playback`, `screen_games`,
   `screen_settings`).
2. Which writer sets the screen index `0x020CCF04` on each `ui_mode_request` (expected: `ui_mode_request`
   itself maps logical mode → screen index).
3. Games: locate the game logic (likely one screen loop plus asset-driven content) and its data.
4. The 148- and 191-entry tables at CPU `0x0208044C` / `0x02081EE4` (mostly `caseD_*`/`FUN_*` targets) —
   large switches, probably the UI drawing/widget code and the video/audio record state machines.
5. Photo orientation: the head bitmap is the transpose of the decoded Y plane; the correction is
   expected upstream in `0x020087E8` / `kc02_load_print_image`.
