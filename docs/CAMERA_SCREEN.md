# KC02 camera screen: decompilation and integration points

Reference for replacing or extending the camera UI while retaining the stock preview and capture pipeline.

Firmware: `firmware/original.bin`, SHA-256
`218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5`.
CPU addresses below use the verified mapping `file_offset = CPU - 0x01FFDA00`.
This analysis did not access the camera or change its flash.

## Files

- [`camera_screen/camera_screen_decompiled.c`](camera_screen/camera_screen_decompiled.c): all 72 exports combined into one 3,270-line file, with address separators.
- [`camera_screen/raw/`](camera_screen/raw/): complete Ghidra output for **72 functions**, including all **33 return-delimited entries in the camera region** `0x020082F4..0x0200AB04`, plus selected framework, widget and LCD dependencies. This is not the complete transitive SDK/driver call graph.
- [`camera_screen/raw/functions.tsv`](camera_screen/raw/functions.tsv): entry addresses, Ghidra labels, export status and temporary-function provenance.
- [`camera_screen/raw/calls.tsv`](camera_screen/raw/calls.tsv): direct call sites and targets.
- [`camera_screen/raw/disassembly.txt`](camera_screen/raw/disassembly.txt): instruction listing for the exported bodies; consult this when a prototype or decompiler expression looks wrong.
- [`camera_screen/camera_screen_model.c`](camera_screen/camera_screen_model.c): compilable reconstruction of all **33 camera-region functions**, including UP/DOWN, capture/save/print and the complete message table. It links against stock drivers/framework routines through fixed-address imports.
- [`camera_screen/camera_screen_sketch.c`](camera_screen/camera_screen_sketch.c): preserved earlier partial, descriptive model with undefined adapters.
- [`camera_screen/REBUILD.md`](camera_screen/REBUILD.md): offline build/test commands, dependency boundary, deliberate differences and deployment limits.
- [`camera_screen/build/manifest.json`](camera_screen/build/manifest.json): source/binary hashes, replacement addresses and unapplied hook candidates; [`build/camera_screen.elf`](camera_screen/build/camera_screen.elf) is the linked OpenRISC module.
- [`camera_screen/abi/`](camera_screen/abi/): 22 additional dependency exports plus instruction/call-site evidence for reconstructed argument contracts.
- [`camera_screen/tables.json`](camera_screen/tables.json), [`widgets.tsv`](camera_screen/widgets.tsv), [`keymap.tsv`](camera_screen/keymap.tsv): decoded firmware data with original addresses and raw widget bytes.
- [`camera_screen/extract_camera_tables.py`](camera_screen/extract_camera_tables.py): reproducible, hash-pinned table extraction.

The rebuilt module compiles and links as little-endian, no-delay OpenRISC: **10,768 bytes**, no undefined symbols, exact original widget-table bytes and a relocated keymap pointing to its own handlers. Host tests pass **6,018 assertions**, including **5,956 differential controller-state cases** against byte-checked original UP/DOWN/LEFT/RIGHT/BACK instructions, with stock callees mocked. These checks do **not** establish target ABI or on-device correctness. The module exceeds the current 5,632-byte RAM payload slot; its example link address is not verified free RAM. No hooks, uploads or flash changes have been applied. The raw files remain decompiler output, not recovered vendor source.

## Architecture

```text
mode_manager_loop (0x02008188)
  enter: mode_photo_enter (0x020082F4)
    encode/crop setup -> dispLayerInit(0) -> hal_lcdPreviewStart
    win_open(camera descriptor) -> construct overlay -> enter message 0x0E
    enable CSI
  service iteration:
    app_main_loop (0x02000404)
      system_tick -> ui_key_dispatch -> video_present(0)
    mode_photo_tick (0x020084A0)
      optional frame effects -> submit LCD frame -> rearm next CSI buffer
  exit: mode_photo_exit (0x020083C8)
    reset camera state / display layer
  destroy old desktop children through the stock mode manager
```

### Preview: hardware capture with a software handoff

`dispLayerInit(0)` (`0x02004588`) obtains the sensor resolution and calls
`hal_lcdPreviewStart` (`0x02039F9C`). The latter configures the CSI/LCD scaler,
Y/UV destination addresses, output routing and LCD DMA. There is no application loop drawing camera pixels one at a time.

The callback at **`0x0203A2F0`** and the mode tick exchange frames:

- `app_context + 0x58` (`0x020D41C4`): enables the camera's task-side frame handoff.
- `app_context + 0x5C` (`0x020D41C8`): handshake state. The callback publishes a frame and writes **0**; the tick writes **1** before acquisition and **2** before submission. Treat these meanings as reconstructed from the control flow.
- **`0x020CD788`**: frame pointer, setter `0x02022700`, getter `0x02022720`.
- Effect group `app_context + 0x59`: value 2 calls `photo_apply_effect` (`0x02022740`), value 3 calls the helper at `0x020460B4`.
- `hal_lcdDealBuffer` (`0x02039E44`) submits the completed frame, with a rotation path when needed.
- **`0x0203A84C`** obtains/configures the **next capture buffer**, sets its CSI destination addresses and kicks DMA. Its inherited name `lcd_commit_video_frame` obscures that role.

Do not omit the callback/tick handshake when replacing the camera mode. The tick also preserves a state=1 return when the frame getter yields null; the readable model retains this branch rather than inventing recovery behaviour.

### Overlay: widget table plus invalidation/paint

Camera window descriptor **`0x020C2B40`**:

```text
+0x00  0x020C2B50  message/handler table
+0x04  0x020C2BD0  widget creation table
+0x08  runtime root-window handle (zero in the flash image)
+0x0C  repeat-open support (zero)
```

`win_open` (`0x02012304`) constructs the window with
`win_create_widget_list` (`0x020250A4`), installs its message map and sends the window-open notification.
The adapter at `0x02012150` translates this notification into the camera's **0x0E** handler,
`0x0200A37C`. That handler updates the existing children rather than constructing each widget manually.

**Firmware widget records are 48 bytes, not the SDK header's 40 bytes.**
`win_create_widget_list` advances by `0x30`, and the string constructor at `0x020283CC`
reads the extra tail at `+0x28..+0x2B` and `+0x2D`. The extraction retains these bytes without guessing their meanings.
The imported SDK `widgetCreateInfor` type is therefore not suitable unchanged for passing a new creation table to this firmware.

The table has a **320x240 root and 18 children**, followed by a terminator at `0x020C2F60`:

| ID | x,y | w,h | Role from table and update handlers |
|---|---|---|---|
| root `0xFFFF` | 0,0 | 320,240 | frame/window root |
| `0x01` | 0,0 | 320,32 | top-bar rectangle |
| `0x00` | 2,0 | 33,32 | fixed icon, resource `0x8000003E` |
| `0x04` | 224,0 | 32,32 | resolution text (`1M` through `48M`, fallback `???`) |
| `0x10` | 128,88 | 64,64 | countdown icon |
| `0x11` | 256,0 | 32,32 | print-option indicator, controlled by `+0x5F` |
| `0x08` | 34,0 | 32,32 | storage-state icon |
| `0x0F` | 66,0 | 32,32 | scene/capture-preset icon |
| `0x0A` | 287,0 | 33,32 | battery/USB-state icon |
| `0x02` | 0,215 | 320,25 | bottom-bar rectangle |
| `0x0C` | 5,215 | 220,25 | RTC text |
| `0x13` | 40,50 | 210,140 | status-dialog rectangle |
| `0x14` | 60,50 | 180,45 | status-dialog title |
| `0x15` | 40,95 | 210,95 | status-dialog message |
| `0x17` | 60,45 | 200,144 | transient indicator, resources `0x80000041/42` |
| `0x16` | 160,0 | 58,32 | zoom text; initial `01X` |
| `0x18` | 260,32 | 68,24 | estimated remaining-photo count; initial `000000` |
| `0x19` | 120,0 | 32,32 | light-state icon |
| `0x1A` | 120,0 | 33,32 | alternate light-state icon |

`winUpdate` (`0x0202A110`) marks windows dirty and increments the counter at `0x020CE0D0`.
`video_present` (`0x0205B8C4`) invokes the paint pass, sends UI message `0x13`, transfers the overlay buffer when configured, and calls `ax32xx_deUpdate`.

Two inherited names are misleading:

- **`0x0202A1D0`, labelled `ui_close_all_windows`, is the pending-window paint pass.** Its loop and dirty-counter logic follow the SDK's `winPaint` structure; it also performs pool diagnostics.
- **`0x02028D78`, labelled `winDestroy`, paints invalid window regions.** It decrements the dirty counter, tests visibility, sends paint message **6** with clipped rectangles and clears the invalid flag. It corresponds to a `winPaint1`-like helper, not destruction. Its imported destructor prototype is wrong.

The raw exports retain these labels for traceability. Do not call an address solely because its current name resembles the desired API.

## Buttons and system notifications

`ui_key_dispatch` (`0x020079B4`) splits a queued word into message ID (low 16 bits) and flags (high 16 bits), then invokes handlers with `(root/context, 1, &flags)`.
It uses the primary vector at **`0x020CCC8C`**, populated by `msgFuncRegister` from the installed window map, and a secondary vector at `0x020CCBD4`.
The camera's source table is `0x020C2B50`. Earlier shorthand distinguishing a system vector from the camera keymap must not imply the camera bypasses the generated primary vector.

| Message | Raw body | Behaviour |
|---|---|---|
| `0x0E` enter | `0200a37c.c` | initialize labels/icons/dialog visibility and transition state |
| `0x0F` exit | `0200947c.c` | cleanup notification and flags |
| `0x10` refresh | `0200a310.c` | refresh resolution, storage, battery and option widgets |
| UP `0x1B` | `02009890.c` | flags=1 increases zoom (limit 15); flags=2 reverses scene/effect sequence |
| DOWN `0x1C` | `02009568.c` | flags=1 decreases zoom (limit 0); flags=2 advances scene/effect sequence |
| RIGHT `0x1E` | `02009f6c.c` | flags=0 cycles three light states; device ioctl `0x20000011` |
| LEFT `0x1D` | `02009de0.c` | flags=0 toggles `app_context+0x5F`, consumed by print/capture logic |
| OK `0x1A`, `0x23` | `0200a4bc.c` | countdown/burst, capture/save, optional printing, audio/light feedback and dialogs |
| short POWER/BACK `0x24` | `02009f08.c` | flags=0 requests main menu (mode 6) |
| `0x01` | `02009e70.c` | storage-state notification/dialog |
| `0x02` | `02008d74.c` | refresh battery/USB icon |
| `0x03` | `02008d3c.c` | conditional battery refresh |
| `0x06` | `02008bd4.c` | no-op |
| `0x0A` | `02009108.c` | RTC and conditional battery/icon update |

UP/DOWN retain inherited `video_key_next/prev` names, although this camera keymap references them.
Their normal flags=0 branch does not change zoom/presets; do not rewrite them as unconditional press handlers.
They traverse groups 0..3, hardware colour presets, software effects and resource-based effects;
software groups and edge presets restrict zoom.

OK's full body includes both storage-backed and memory-backed capture/print branches. A replacement UI can retain this stock handler initially rather than reimplementing the capture, printer, audio and allocation contracts at once.

## Confidence and remaining work

The table addresses, stride, geometry, direct calls and control-flow guards come from the firmware bytes/disassembly. Descriptive role names and state meanings are reconstructions. Hardware camera-mode testing still awaits reliable power.

The decompiler emits overlap/calling-convention warnings. Several imported SDK prototypes lose arguments or show artifacts such as `register0x00000401`, `&stack0x00000000`, and unaffiliated registers. These are analysis defects, not source code to copy into a payload.

The export ran with `-readOnly -noanalysis` under `/tmp/kc02-board-re/ghidra.lock`. It created missing function definitions only in the transient program and discarded them on exit; it did not change the shared Ghidra project. The pre-existing label at `0x02008380` is inside the actual `mode_photo_enter` body (`0x020082F4..0x020083C8`), not an independent callback to hook.

For a custom UI, preserve stock sensor/preview setup and the frame handshake first. Replace the window descriptor, 48-byte widget table and selected handlers only after verifying their call ABI and allocation/lifetime rules. Keep global shutdown/USB hooks through the stock dispatcher.
