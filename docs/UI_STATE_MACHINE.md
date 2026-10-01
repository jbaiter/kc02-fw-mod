# KC02 UI state machine — reference map

Target: **HiMont KC02 (U8) kids instant-print camera**, firmware
`original_firmware_backup.bin` (md5 `7236c4c1bd3f12b1d85d33a9bebdf510`).

This document is the durable map of the UI/state machinery, written for future
customization (child-friendly boot-to-camera, adult-only menu gate, and the
USB/BOT hoist that shipped with it).

**Conventions**

* Addresses are **CPU** addresses. `CPU = 0x02000000 + file_offset - 0x2600`
  (verified: `0x02000000` is app code, file offset `0x2600`).
* `CONFIRMED` = a listing/decompilation or raw-image byte read is quoted.
* `INFERRED` = consistent with the evidence but not directly proven.
* Ghidra symbols named here are applied to the analysis project and are
  replayable from `tools/ghidra_symbols.tsv` (`python3 tools/ghidra_symbols.py
  --verify`).
* Every routine below was reached read-only. Flash write/erase routines
  (`spi_sector_erase_DANGEROUS 0x0203d584`, `spi_page_program_DANGEROUS
  0x0203d454`, `spi_write_DANGEROUS 0x0203d4e8`, `spi_wren 0x0203d25c`,
  `fw_upgrade_task_DANGEROUS_erase_program 0x02002418`) are named, never invoked.

---

## 1. Overview

There is **no global "screen" enum**. There are two independent layers:

1. a **mode machine** (`ui_mode_machine_walker` @ `0x02008188`) that picks one of
   ten *mode records* and calls three function pointers per record
   (`enter` / `exit` / `frame`), and
2. a **UI state** byte (`ui_state_var` @ `0x020d4173`) that each mode sets, and
   which `ui_render_dispatch` @ `0x02004944` turns into one of six *renderers*
   (or the special state 7).

```
                      power-on
                         |
             sys_app_init (0x020000bc)
                         |  ui_mode_table_init(0x02000000) registers 10 records
                         v
             ui_app_task (0x0200046c)
                         |  ui_settings_apply_all(0x02006dec)
                         |  fw_upgrade_task_DANGEROUS (0x02002418)   <- guarded
                         |  initial mode = 10 if usb_sense_state==2 else 6
                         |  key_hook_install_system(0x020c2a64)
                         v
        +---------------------------------------------+
        |  ui_mode_machine_walker  (0x02008188)       |
        |    while (ui_mode_pending != 0xb):          |
        |      run EXIT (rec+0x0c)                    |
        |      rec = ui_mode_table[pending]           |
        |      run ENTER (rec+0x08)                   |
        |      while (ui_mode_pending > 0xb):  <----+ |
        |        ui_frame_hook (0x02000404) --------+ |   <- once per frame,
        |        run FRAME (rec+0x10)                 |      EVERY mode
        +---------------------------------------------+
                         |
   ui_frame_hook body:   FUN_0202bc7c()
                         usb_sense_task(1)   (0x0200d2c0)
                         key_event_dispatch() (0x020079b4)
                         FUN_0205b8c4(0)

   key_event_dispatch:   event = key_event_queue_pop(0x020d41e0)
                         key_hook_table_primary[event]()      (0x020ccc8c)
                         key_hook_table_system[event]()       (0x020ccbd4)

   a handler typically calls
        ui_render_dispatch(state)  ->  ui_state_renderer_table[state]()
        ui_mode_request(mode, 1)   ->  sets ui_mode_pending
```

`CONFIRMED` for every arrow above: `0x02000528 l.jal 0x02008188`;
`0x020082a4 l.jal 0x02000404`; `0x020082bc l.jalr r4` with
`r4 = *(ui_mode_active_rec+0x10)`; `0x02000410/0x02000418/0x0200041c/0x02000424`
inside `ui_frame_hook`; `0x020079b4` (decompiled) for the two-table key
dispatch.

### The one place that runs every frame, in every mode

`ui_frame_hook` @ **`0x02000404`** (52 bytes, `0x02000404..0x02000437`) is called
exactly once per UI frame from `ui_mode_machine_walker`'s inner loop at
`0x020082a4`, and it has **exactly one caller** (Ghidra `references(to=
0x02000404)` → `0x020082a4`). That makes it the only correct hook point for
"run X in every mode". This is what `tools/kc02_usb.py usb-hoist` patches (see
§7).

---

## 2. The mode system

### 2.1 Table and bookkeeping (all RAM)

| Address | Ghidra name | Meaning | Evidence |
|---|---|---|---|
| `0x020ccd5c` | `ui_mode_table` | 12 × u32 mode-record pointers | `ui_mode_table_register` `(&DAT_020ccd5c)[mode]=rec` |
| `0x020ccd50` | `ui_mode_pending` | pending mode: `0xc` idle/running, `0xb` quit, `1` = mode changes blocked | `0x020081d8/0x02008234`, decompiled walker |
| `0x020ccd54` | `ui_mode_current` | currently executing mode | `0x02008228` |
| `0x020ccd58` | `ui_mode_active_rec` | pointer to the *active* record (patched by `unlock-usb`) | `0x02008240` |
| `0x020ccd4c` | `ui_mode_previous` | mode that was current before the last successful request | `0x02008150-0x02008168` |

`CONFIRMED`. Note the table itself is *RAM*; the records it points at are in
flash/rodata and cannot be rewritten by a RAM poke, which is why
`unlock-usb` redirects the slot to a RAM copy.

**All 12 entries default to 0 at runtime.** The image bytes at
`0x020ccd44..0x020ccdd4` (and at `0x020d416c..`) are the *compressed asset tail*,
not initial values — do not read them as data (`0x020c3348`/`0x020c334c` are the
only plausible-looking values there).

### 2.2 Record layout (`CONFIRMED`, all ten records)

```
+0x00  char*  display name (rodata string)
+0x04  u32    arg   -> passed to enter/frame; also written by ui_mode_request
+0x08  fn*    enter
+0x0c  fn*    exit
+0x10  fn*    frame       <- dispatched once per frame by the walker
+0x14  u32    per-mode data (key-hook list pointer for the UI modes; mode-private
+0x18  u32    per-mode data  state blob for modes 2/3, which do not use hook lists)
+0x1c..+0x24  per-mode data
```

### 2.3 The ten records (`CONFIRMED`; UI identity `INFERRED` where marked)

| index | record | name string | enter | exit | frame | UI identity |
|---|---|---|---|---|---|---|
| 1 | `0x020c3334` | `"Power Off"` `0x0207ba7c` | `0x0200c658` | `0` | `0` | power-down screen — CONFIRMED |
| 2 | `0x020c33b0` | `"Video Recorde[r]"` `0x0207ba96` | `0x0200dfe8` | `0x0200d860` | `0x0200d620` | video recorder — CONFIRMED name |
| 3 | `0x020c2ad4` | `"Photo Encode"` `0x0207b9f2` | `0x020082f4` | `0x020083c8` | `0x020084a0` | **the camera** (see below) |
| 4 | `0x020c2f90` | `"Play Back"` `0x0207ba60` | `0x0200aff4` | `0x0200ab04` | `0x0200b06c` | playback — CONFIRMED name |
| 5 | `0x020c4ef0` | `"Audio Player"` `0x0207bbc4` | `0x020146d4` | `0x02014714` | `0x02014738` | audio player — CONFIRMED name |
| 6 | `0x020c4c18` | `"Main menu"` `0x0207bb53` | `0x020130e4` | `0x020130d0` | `0x020130bc` | main menu — CONFIRMED name |
| 7 | `0x020c4e1c` | `"Game menu"` `0x0207bb90` | `0x02013aa0` | `0x02013a8c` | `0x02013a78` | game menu — CONFIRMED name |
| 8 | `0x020c64fc` | `"Game"` `0x0207bec0` | `0x02020530` | `0x0201f020` | `0x020201f8` | game — CONFIRMED name |
| 9 | `0x020c5408` | `"Setting menu"` `0x0207bbe4` | `0x02015db4` | `0x02015da0` | `0x02015d8c` | settings — CONFIRMED name |
| 10 | `0x020c339c` | `"usb device"` `0x0207ba8b` | `0x0200d498` | `0x0200d450` | `0x0200d588` | **USB lock screen** — CONFIRMED |

Indices **0 and 11 are never registered** (`ui_mode_table_init` sets
1,2,3,4,5,10,6,7,8,9). The walker treats a NULL record as a fallback to
`ui_mode_table[1]` (`0x0200824c-0x02008258`).

### 2.4 Which mode is "the camera" — cross-validated

Three independent facts agree:

1. mode 3's record is named **"Photo Encode"** and its enter `ui_mode3_photo_enter`
   `0x020082f4` sets `ui_state_var = 0` and calls `ui_render_dispatch(0)`
   (`0x02008368 sb 0x7(r14),r2` with `r2=0`, then `0x0200836c l.jal 0x02004944`;
   `CONFIRMED`).
2. renderer slot 0 (`ui_render_state_0` `0x0200411c`) is the full-screen
   viewfinder layer; the firmware's own legacy label script
   (`tools/KC02Labels.java`) calls it `handler_camera`.
3. the main-menu OK handler picks mode 3 for item index 0 (§4.4).

⇒ **boot-to-camera = make the initial mode 3 instead of 6** (see §7).

`ui_mode3_photo_frame` `0x020084a0` is the capture/shutter state machine; it keys
off `0x020d41c8` (byte `0x5c`), `0x020d41c5` (`0x59`) and drives `FUN_02022720`
/ `FUN_02022740` / `FUN_020460b4` (`CONFIRMED` listing `0x020084A0-0x02008524`).

### 2.5 Mode 10 (the USB lock screen) in full

| Item | Address | What it does |
|---|---|---|
| enter | `0x0200d498` | `DAT_020ccd8c=1`; `FUN_020041b0()`; `ui_render_dispatch(7)`; `FUN_02006f88(0x60 or 0x61)`; `FUN_02054c94(w,h)`; spin on `FUN_020524a0()`; `usb_select_mode(0 or 1)`; `key_hook_install_primary(0x020c3354)`. `CONFIRMED` (`0x0200d498-0x0200d54c`) |
| frame | `0x0200d588` | `ret = bot_service_wrapper()` at **`0x0200d598`**; if `ret != 0` → `hal_sense_read(...)` + `ui_mode_request(0xb, ret)`. `CONFIRMED` (`0x0200d588-0x0200d5e0`) |
| exit | `0x0200d450` | `DAT_020ccd8c=0`; `FUN_0204a9a4()` = **USB interface down**; `FUN_020403ec`; `FUN_02034ee4`; `FUN_02004588(1)`. `CONFIRMED` (`0x0200d450-0x0200d494`) |
| key hooks | list `0x020c3354` | events `0x1,0x2,0xa,0x1a,0x1b,0x1c,0x23,0x27` → `0x0200d420`, `0x0200d5e4`, `0x0200d438`, `0x0200d3d8`×2, `0x0200d3f0`, `0x0200d408`, `0x0200d550`. Five of the seven are `return 0` stubs ⇒ **every camera button is swallowed**. `CONFIRMED` |

That is the whole lock: mode 10 has no camera UI and no working buttons, and it
is the only mode that services the BOT transport.

---

## 3. The UI state renderer table

### 3.1 Dispatcher

`ui_render_dispatch` @ `0x02004944` (`CONFIRMED`, listing `0x02004944-0x020049a4`):

```
r3 = state & 0xff
if (state == 7)   FUN_0203a590(0);           return 0
if (state >  5)   return -1
ui_state_renderer_table[state]();            return 0
```

`ui_state_set` @ `0x020049a8` writes `ui_state_var` (`0x020d4173`) then calls the
dispatcher — that byte is the "current UI state" you would poke to force a
layout.

### 3.2 Table contents and the `0x0207B908..0x0207BA00` region — resolved

The region holds **two different, adjacent tables**. Ghidra's
`PTR_FUN_0207b908` label covers both because the decompiler merged them.

```
0x0207B908  ui_state_renderer_table   <- used by ui_render_dispatch (base 0x0207B908, index <= 5)
  [0] 0x0200411c   ui_render_state_0   camera        <- mode 3
  [1] 0x02003e74   ui_render_state_1   video         <- mode 2
  [2] 0x02003ef0   ui_render_state_2   audio player  <- mode 5
  [3] 0x02003f90   ui_render_state_3   playback      <- mode 4
  [4] 0x02004014   ui_render_state_4   games         <- mode 7
  [5] 0x02004098   ui_render_state_5   settings      <- mode 9
0x0207B920  ui_settings_handler_table <- used by ui_settings_dispatch (base 0x0207B920, index = id-7, <= 0x1c)
  ... 29 entries: 0x02006D58, 0x02006DD4 (x N), 0x02006CCC, 0x02006D00, ...
0x0207B994  rodata: "exmend.bin", "29X_V1.0.0", audio sample-rate table, "JXR_20220309", "SELFTEST.bin", ...
0x0207BA00  init_callback_table (10 pointers)  -> 0x020093AC .. 0x020093F4
```

`CONFIRMED`: `ui_render_dispatch` computes `r2 = 0x0207B908` (`0x02004980 ori
r2,r2,0xb908`) and indexes at `state<<2`; `ui_settings_dispatch` computes
`r3 = 0x0207B920` (`0x02006CAC ori r3,r3,0xb920`) and indexes at `(id-7)<<2`.
So the `0x02006Dxx` words are **settings case bodies inside
`ui_settings_dispatch`'s own body**, not renderer thunks and not a jump table
sharing the renderer table. The real `ui_state_renderer_table` has **exactly six**
entries.

The table order matches the main-menu order and the mode identity list exactly
(item 0 → mode 3 → state 0 = camera, …, item 5 → mode 9 → state 5 = settings),
which is a strong independent confirmation of both mappings.

### 3.3 What each renderer builds

Signature of the layer primitive: `gfx_layer_setup(layer=r3, type=r4, x=r5,
y=r6, w=r7, h=r8, flag=[sp+0])`; `gfx_get_screen_size(&w,&h)`; `gfx_set_layer_mode(x)`.
Screen is 320×240 (`w=0x140`, `h=0xf0`) per the menu objects.

| state | renderer | calls (`CONFIRMED` by listing) |
|---|---|---|
| 0 camera | `0x0200411c` | `set_layer_mode(1)`; `layer_setup(1,2,0,0,w,h,1)`; `FUN_020517e0()` (OSD/icons) |
| 1 video | `0x02003e74` | `set_layer_mode(1)`; `layer_setup(1,3,0,0,w,h,1)`; `layer_setup(0,2,0,0,w,h,1)` |
| 2 audio | `0x02003ef0` | `set_layer_mode((w-((w-160)&~7))&0xffff)`; `layer_setup(1,3,0,0,w,h,1)`; `layer_setup(0,2,(w-160)&~7,0x14,(w-that),0x78,1)` |
| 3 playback | `0x02003f90` | `set_layer_mode(1)`; `layer_setup(1,2,w-160,0x14,0xa0,0x78,..)`; `layer_setup(0,3,0,0,w,h,..)` |
| 4 games | `0x02004014` | `set_layer_mode(0)`; `layer_setup(1,3,0,0,w>>1,h,1)`; `layer_setup(0,2,0,0,w>>1,h,1)` |
| 5 settings | `0x02004098` | `set_layer_mode(0)`; `layer_setup(1,3,w>>1,0,w>>1,h,1)`; `layer_setup(0,2,0,0,w>>1,h,1)` |

State 7 is special-cased to `FUN_0203a590(0)`; both mode 6 (main menu) and mode
10 (USB lock) render it, so **state 7 is a shared "base/blank + UI object"
screen**, not an SD-card-specific one. The visible difference between them comes
from each mode's key-hook list and UI object, not from the renderer.

---

## 4. Key input path

### 4.1 Physical buttons → event ids

The panel is an **ADC resistor ladder** on one analogue pin; the "ad-key" driver
struct is at `0x020c2644` (`CONFIRMED`, raw bytes) and the threshold table at
`0x020c2680` (`CONFIRMED`), 7 entries × 8 bytes `[u32 reserved][u16 key_id][u16
adc_center]`:

| key_id | button | adc center |
|---|---|---|
| `0x1a` (26) | **OK** | 386 |
| `0x1e` (30) | **RIGHT** | 776 |
| `0x1d` (29) | **LEFT** | 252 |
| `0x1b` (27) | **UP** | 657 |
| `0x1c` (28) | **DOWN** | 510 |
| `0x24` (36) | **POWER** | 12 |

> `docs/FIRMWARE_NOTES.md` quotes these as file offsets `0x0C4C80..`; they are
> the same bytes but the CPU addresses are `0x020C2680..0x020C26BB`. The old
> notes' "key scan function `0x004CC8`" is **wrong**: file `0x4CC8` maps to CPU
> `0x020026C8`, which is part of the **flash-update** path (it calls
> `spi_write_DANGEROUS 0x02003d4e8` at `0x020026dc`). Do not use it as a keypad
> routine.

### 4.2 Event delivery

An event word is `(payload << 16) | id`, pushed into the ring buffer at
`0x020d41e0` (`key_event_queue`; `key_event_queue_push` `0x02021700`, pop
`0x0202173c`, count `0x020217cc`).

Every frame, `ui_frame_hook` → `key_event_dispatch` `0x020079b4`:

```
ev = pop(key_event_queue)                     // status byte must be 0, else return
event = ev
if ((ev & 0xffff) > 0x2d) return              // 0x020079ec b.sfgtui r2,0x2d / b.bf exit
id = ev & 0xffff; payload = ev >> 16
store payload at [sp+4]                       // 0x02007a00 b.sw 4(r1),r11  <- what handlers get
if (id-0x1a <= 0x13) goto dispatch            // KEY events 0x1a..0x2d go STRAIGHT to the tables
if (payload != 0)    goto dispatch            // other events that carry a payload: same
// ... else the modal/mode side path (ui_modal_is_open, mode != 4, mode != 5, id 0x26 ...)
//     which itself always ends at the dispatch below
dispatch:
    h = key_hook_table_primary[id]; if (h) h(payload, 1, &ev)    // 0x02007a88..0x02007a98
    h = key_hook_table_system [id]; if (h) h(payload, 1, &ev)    // 0x02007ab4..0x02007ac4
```
`CONFIRMED` - decompiled **and** re-read instruction-by-instruction at
`0x020079b4-0x02007adc`. **Both** tables are consulted for the same event, so a
mode overlay can add behaviour without removing the global one.

Every hook is invoked with `r3 = payload` (`b.srli r11,r11,0x10` then
`b.ori r3,r11,0x0`), `r4 = 1` and `r5 = &ev`, and with `r9` set to the return
address inside the dispatch (`l.jalr r14` at `0x02007a98` / `0x02007ac4`).  That
triple is all a handler gets: the *only* per-event information beyond the id is
`payload` (the word at `*(r5)`), which the stock handlers use to tell a press
(`!= 0`) from the rest.

> **Corrigendum: "payload-carrying key events are dropped".**  An earlier note in
> this project claimed that key events `0x1a..0x2d` with `payload != 0` are
> dropped *before the handlers run*, and the child lock was designed around that
> (two taps instead of a long press).  Listing work on `0x020079b4` shows the
> claim is **wrong - nothing is dropped**: `b.sfleui r4,0x13` + `b.bf
> 0x02007a10` sends every key event straight to the hook dispatch at
> `0x02007a6c`; the `payload` test at `0x02007a1c`/`0x02007a20` only decides
> whether the modal side path runs for the *non-key* events that fall through.
> The queue push/pop (`0x02021700`/`0x0202173c`) filter nothing either.
> Independent functional proof: `key_mode3_power_request_menu` only acts when
> `*(r5) != 0` (a press), so if payload-carrying key events never reached the
> hooks, the POWER button could not open the menu at all.  A payload/long-press
> design is therefore *possible*; the shipped child lock still uses the
> specified two-tap sequence (UP, then POWER), which needs no such assumption.

### 4.3 Hook tables and installation

| Address | Name | Notes |
|---|---|---|
| `0x020ccc8c` | `key_hook_table_primary` | per-mode overlay; rebuilt by `key_hook_install_primary` |
| `0x020ccbd4` | `key_hook_table_system` | installed once at boot from `0x020c2a64`; persists across modes |
| `0x020c2a4c` | `key_hook_list_default` | base list for the primary table: events `0x2`,`0x3` → `return 0` |
| `0x020c2a64` | `key_hook_list_system` | `0x1,0x2,0x3,0xa,0x1a,0x1b,0x1c,0x21,0x22,0x23,0x24,0x26,0x28` |
| `0x02007880` | `key_hook_install_primary` | clears the table (0xb8 B = 0x2e slots), installs the default list, overlays the argument list |
| `0x02007938` | `key_hook_install_system` | clears and installs the argument list |
| `0x02012304` | `ui_object_start` | installs `obj[+0x00]` as the primary hook list (`0x020123ac l.jal 0x02007880`) and starts the UI node `obj[+0x08]` |

List format = `(u32 id, u32 handler)` pairs, terminated by `id >= 0x2e` or
`handler == 0` (`CONFIRMED` from the decompiled installers).

Per-mode hook lists (`CONFIRMED`, read from the image):

| mode | object / list | events covered |
|---|---|---|
| 3 camera | `0x020c2b40` → list `0x020c2b50` | `0x1,0x2,0x3,0x6,0xa,0xe,0xf,0x10,0x1a,0x1b,0x1c,0x1d,0x1e,0x23,0x24` |
| 6 main menu | `0x020c4c2c` → list `0x020c4c3c` | `0xe,0xf,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x23,0x9,0xa` |
| 7 game menu | list `0x020c4e40` | `0xe,0xf,0x1d,0x1e,0x1a,0x23,0x24,0x9,0xa` |
| 9 settings | list `0x020c542c` | `0xe,0xf,0x10,0x1d,0x1e,0x22,0x1a,0x23,0x24,0x9,0xa` |
| 4 playback | list `0x020c2fb4` | `0xe,0xf,0x10,0x24,0x1b,0x1c,0x1d,0x1e,0x1a,0x1,0x2,0x3,0x6,0xa,0xd` |
| 8 game | list `0x020c6520` | `0xe,0xf,0x1b,0x1c,0x1a,0x23,0x24,0xb` |
| 10 usb | list `0x020c3354` | `0x1,0x2,0xa,0x1a,0x1b,0x1c,0x23,0x27` |

### 4.4 Actions

**Main menu (`ui_mode6_*`).** The menu is a **3×2 grid of six items**; the
selection index is `ui_main_menu_index` (`0x020d41d3`, `CONFIRMED`).

| key | handler | effect |
|---|---|---|
| LEFT `0x1d` | `key_mode6_left` `0x0201379c` | index −1, wraps below 0 → 5 |
| RIGHT `0x1e` | `key_mode6_right` `0x02013714` | index +1, wraps past 4 → 0 |
| UP `0x1b` | `key_mode6_up` `0x020138b0` | `≥3 ? −3 : +3` (row move) |
| DOWN `0x1c` | `key_mode6_down` `0x02013828` | `≤2 ? +3 : −3` (row move) |
| OK `0x1a` / `0x23` | `key_mode6_ok_select` `0x02013938` | `ui_mode_request(mode_for_index)` |
| event `0x1f` | `key_mode6_event1f_playback` `0x02013a14` | `ui_mode_request(4)` (playback) |
| event `0x9` | `0x02013698` | cursor blink / animation tick |

`mode_for_index` (`CONFIRMED`, switch at `0x02013988-0x020139f0`):

| index | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| mode | **3 camera** | 2 video | 5 audio | 4 playback | 7 game menu | 9 settings |

**Entering the menu.** The only route into mode 6 from the UI is the camera's
POWER/MENU action (§below); the boot path also defaults to mode 6 when USB is
not attached.

**"Back" from the camera UI.** There is no dedicated *back* button. On the
camera screen the **POWER/MENU button (`0x24`) short press** is the escape:

```
key_mode3_power_request_menu  0x02009f08
    if (event flag == 1 && payload != 0) {
        ui_mode_request(6);            <- 0x02009f30 l.jal 0x02008108  (r3 = 6)
        ... touch 0x020d41d4 / 0x020d41d5
    }
```
`CONFIRMED` (`0x02009f08-0x02009f64`). So **camera → Main menu (mode 6)**, whose
own OK handler then selects the next destination mode. Nothing calls
`ui_mode_previous` for the camera, so leaving the menu does not automatically
return to the camera; you re-enter it via menu item 0 (mode 3).

**Which handler belongs to which event — pair the ids in the hook list, not the
label.** The mode-3 hook list `0x020c2b50` holds `(id, handler)` pairs; the two
rows that matter for the child lock are `CONFIRMED` straight from the image:

| list word | id | handler |
|---|---|---|
| `0x020c2b68` / `0x020c2b6c` | `0x1b` **UP** | `0x02009890` |
| `0x020c2b98` / `0x020c2b9c` | `0x24` **POWER** | `0x02009f08` |
| `0x020c2ba0` / `0x020c2ba4` | `0x01` | `0x02009e70` (the overlay opener) |

An earlier revision of this document paired event `0x1b` with `0x02009e70`; that
was a mis-pairing (the function *behaviour* was right, its event id was not).
`tools/kc02_unlock.py stock-check --image <dump>` re-verifies both bindings.

**Other camera keys** (`CONFIRMED`):

| key | handler | effect |
|---|---|---|
| OK `0x1a`/`0x23` | `key_mode3_ok` `0x0200a4bc` | capture/shutter state machine over `0x020d41c7` |
| LEFT `0x1d` | `key_mode3_left` `0x02009de0` | toggles `0x020d41cb`, advances `0x020d41c7` to 7 |
| RIGHT `0x1e` | `key_mode3_right` `0x02009f6c` | cycles `0x020cd98` 0/1/2 and pokes MMIO reg `0x02000011` |
| UP `0x1b` | `key_mode3_up_handler` `0x02009890` | camera sub-state/gain handler over the mode-3 state blob at `0x020d416c+0x59/0x5b/0x5e/0x5f/0x6d`; **this is the mode-3 entry for event id `0x1b`** |
| event `0x01` | `key_mode3_event01_overlay` `0x02009e70` | `ui_object_start(0x020c4434, 2\|3)` → overlay/OSD object |
| DOWN `0x1c` | `key_mode3_down` `0x02009568` | dispatch target from the list (semantics not traced) |

**Global (system-table) handlers** — active in *every* mode (`CONFIRMED`):

| event | handler | effect |
|---|---|---|
| `0x2` USB | `key_event_usb_connect` `0x02007ee4` | if `usb_sense_state == 2` → `ui_mode_request(10,1)` @ `0x02007f5c` |
| `0x3` | `0x02007e5c` | `ui_object_start(0x020c4434, 2)` (overlay) |
| `0xa` | `key_event_idle_timeout` `0x02007da8` | decrements `0x020ccd48`/`0x020ccd44`; when either hits 0 → `ui_mode_request(1)` (**Power Off**) |
| `0x26` | `0x02007e20` | `ui_mode_request(1)` (Power Off) |
| `0x1a,0x1b,0x1c,0x21,0x22,0x23,0x24,0x28` | `0x02007c04`/`0x02007c40`/`0x02007c7c`/`0x02007cb8`/`0x02007cf4`/`0x02007d30`/`0x02007d6c` | all call `sys_key_activity_autopoweroff(1)` `0x0200cce8` = "reset the idle auto-power-off countdown". Reaching the timeout also requests mode 1 |

So **Power Off is mode 1**, reached from the global idle timer and from event
`0x26`, not from a per-mode handler.

**Modal gate.** `ui_modal_is_open` `FUN_020111a0` returns the byte at
`0x020cce74`; while it is non-zero `key_event_dispatch` still dispatches but
skips the `FUN_0200ce14()` side path. Anything that needs to be "blocked while a
dialog is up" opens this.

---

## 5. Boot sequence and the initial mode

```
power-on
  sys_app_init        0x020000bc   -> ui_mode_table_init 0x02000000 (0x020000f4)
                                     registers task 0x02021824 (100-tick) etc.
  ui_app_task         0x0200046c
      sys_app_init()                                   0x02000480
      ui_settings_apply_all()                          0x02000484
      fw_upgrade_task_DANGEROUS_erase_program()        0x02000488   <- guarded, never invoked by tools
      FUN_020450d0()                                   0x02000490
      mode = (usb_sense_state == 2) ? 10 : 6           0x02000494-0x020004b0
      ui_mode_request(mode, arg)                       0x020004b4
      key_hook_install_system(0x020c2a64)              0x020004c0
      0x020cc6a0 = 1 ; usb_reenumerate(...)            0x020004d0-0x02000500
      usb_select_mode / re-enumerate for state 0       0x02000510-0x02000524
      ui_mode_machine_walker()                         0x02000528  (never returns until quit)
```
`CONFIRMED` (`0x0200046c-0x02000543`).

* `usb_sense_state` is `0x020d416e`; the value `2` means "a host is driving BOT",
  set by `usb_sense_mode_machine` (`0x0200c8d0`) when
  `bot_service_wrapper()`/`FUN_0204accc()` reports traffic.
* The **SD-card-symbol USB lock** is therefore entered on *every* boot where the
  cable is already attached and the host is talking, and at runtime through
  event `0x2` → `ui_mode_request(10,1)`.
* The `child-lock` patch changes only the `else` branch at `0x020004ac`
  (mode 6 → mode 3), i.e. the initial mode when no host is driving BOT; the
  `usb_sense_state == 2` branch still selects mode 10 (§7.1).

`init_callback_table` `0x0207BA00` (10 pointers to `0x020093ac..0x020093f4`) is a
table of tiny thunks that each set `r2` to `0x4e..0x57` and jump to a common body
at `0x020093f8`; `0x020093a4` is the "call *[r5]" helper the table is walked
with (`CONFIRMED`). These are OS/task registration callbacks, not UI.

---

## 6. Runtime globals index

All of these are **BSS at runtime** (the image bytes at `0x020d0c00+` are asset
tail). `ui_`/`usb_`/`key_` labels below are applied to the Ghidra project.

| Address | Name | Meaning | Evidence |
|---|---|---|---|
| `0x020d4173` | `ui_state_var` | UI state 0..7 read by `ui_render_dispatch` | `0x020049c0 sb 0x7(r4),r3`, r4=`0x020d416c` |
| `0x020d41d3` | `ui_main_menu_index` | selected main-menu item 0..5 | `0x02013740/0x020137c8/0x02013854`… |
| `0x020d416c` | `sys_power_source_state` | sense reg `0x20000003`; `0` → `ui_mode_request(1)` | `usb_sense_mode_machine` decompiled |
| `0x020d416e` | `usb_sense_state` | `0` idle, `1` connected, `2` host driving BOT | `0x0200c8d0` decompiled |
| `0x020d4170` | (unlabelled) | USB/charger mode byte | `usb_mode_machine`/`FUN_0200d0cc` |
| `0x020d41e0` | `key_event_queue` | key/UI event ring buffer (pushed by the ADC driver path) | `key_event_dispatch` + `0x02021700/0x0202173c` |
| `0x020ccd50/54/58/4c/5c` | mode machine slots | see §2.1 | walker listings |
| `0x020ccc8c` / `0x020ccbd4` | key hook tables | see §4.3 | installers |
| `0x020cced0` / `0x020cced4` | `ui_menu_cursor_blink` / `_tick` | menu cursor animation | `0x02013698` |
| `0x020cce74` | (unlabelled) | `ui_modal_is_open` flag | `FUN_020111a0` |
| `0x020ccd8c` | `DAT_020ccd8c` | "a UI mode is active" flag set by mode enter, cleared by exit | `0x0200d4b0` / `0x0200d468` |
| `0x020ccdcc` / `0x020ccdd0` | auto-power-off accumulator / last tick | `sys_key_activity_autopoweroff` | decompiled |
| `0x020ccd44` / `0x020ccd48` | idle timeout counters | `key_event_idle_timeout` | `0x02007da8` |
| `0x020d41c7` etc. | mode-3 capture state bytes (`0x5b`…`0x5f`) | camera sub-state | mode-3 handlers |

---

## 7. Patch points for future work

| Goal | Where | Kind | Status |
|---|---|---|---|
| **USB/BOT hoist** — service the transport in every UI mode | code word `0x02000404` (`ui_frame_hook` entry) → `l.j 0x020d9200`; RAM stub at `0x020d9200` | **1 code word + 36 B RAM** | **implemented** (`tools/kc02_usb.py usb-hoist`, `unlock-usb --hoist`) |
| **Mode-10 bypass** — camera UI + transport while locked | `ui_mode_table[10]` slot `0x020ccd84` + `ui_mode_active_rec` `0x020ccd58` + `ui_mode_pending` `0x020ccd50` + RAM record `0x020d9100` | RAM pointer/data only | implemented (`unlock-usb`, previous run) |
| **Boot straight into the camera** | `0x020004ac` — the `else` branch of the initial-mode select, `b.addi r3,r0,0x6` (`06 00 60 9c`) → `b.addi r3,r0,0x3` (`03 00 60 9c`) | **1 code word** (allowlisted) | **implemented** — `tools/kc02_usb.py child-lock` (part 1); no stub needed.  Leaves the `usb_sense_state==2 → mode 10` branch alone (pair with `unlock-usb`/`usb-hoist`) |
| *(data-only alternative for the same goal)* | write `ui_mode_pending = 3` after boot, or point `ui_mode_table[6]` at a RAM record whose enter is mode 3's | RAM only | **not viable as a boot fix**: the boot path rewrites `ui_mode_pending` and the mode records are in flash.  `child-lock` still writes `ui_mode_pending = 3` once **as a transient live jump** into the camera |
| **Adult-only menu gate** | hijack the `key_mode3_power_request_menu` entry `0x02009f08` → RAM gate stub `0x020d9400`; hijack the mode-3 UP handler entry `0x02009890` (event `0x1b`) → RAM arm stub `0x020d9440`; two RAM bytes at `0x020d9480/81` | **2 code words + 76 B RAM + 4 B RAM** | **implemented** — `tools/kc02_usb.py child-lock` (see §7.1) |
| Force a UI state directly | poke `ui_state_var` `0x020d4173` then trigger a render | RAM only | available (state is re-set by each mode's enter/frame, so it is transient) |

The hoist's exact bytes and its disassembly proof are in
`tools/hoist_roundtrip.txt`; the child lock's are in
`tools/childlock_roundtrip.txt`.  The patch guard admits **only** the four
enumerated `(address, length)` pairs in `tools/kc02_unlock.py::CODE_PATCH_SITES`
(`0x02000404+4`, `0x020004ac+4`, `0x02009f08+4`, `0x02009890+4`) in the code
region - there is no widened range.

### 7.1 The child lock (`tools/kc02_usb.py child-lock`)

Goal: the camera boots into the camera, and a child cannot reach the menu.

```
power-on
  ui_app_task 0x020004ac: else-branch now loads mode 3 (camera)   [code word 1]

camera (mode 3), POWER tap (event 0x24)
  hit hook list 0x020c2b50 -> key_mode3_power_request_menu 0x02009f08
  0x02009f08: l.j 0x020d9400                                      [code word 2]
  GATE STUB 0x020d9400:
      if CHILDLOCK_SEQ  (0x020d9480) != 0 -> clear it, PASS
      else if CHILDLOCK_FLAG (0x020d9481) == 0 -> FAIL -> l.jr r9 (event ignored)
      PASS: re-execute the displaced `b.sw -0x4(r1),r9`, l.j 0x02009f0c

camera (mode 3), UP tap (event 0x1b)
  hit hook list 0x020c2b50 -> mode-3 UP handler 0x02009890
  0x02009890: l.j 0x020d9440                                      [code word 3]
  ARM STUB 0x020d9440:
      CHILDLOCK_SEQ = 1
      re-execute the displaced `b.sw -0xc(r1),r18`, l.j 0x02009894
```

* **UX:** tap **UP**, then press **POWER** → the Main menu opens.  Any other
  POWER press is swallowed by the gate (no mode change at all).  The UP tap also
  still does whatever it did before (the hijack re-executes the displaced
  instruction and jumps back into the stock handler), and so does every normal
  POWER press that passes the gate.
* **Adult override:** `child-lock adult-on` sets `CHILDLOCK_FLAG`, so one POWER
  press opens the menu (`adult-off` clears it again, and also drops a pending
  arm).  The flag and the arm are plain RAM bytes - power-cycling resets them,
  which is the safe default.
* **Both hijacks use `l.j`, never `l.jal`**: no link register is set, so `r9`
  still holds `key_event_dispatch`'s return address and the stub's fail path is
  a bare `l.jr r9` (no stack traffic, `r3/r4/r5` untouched for the pass path).
  Only `r6/r7` are used - both are scratch-safe across calls in this firmware.
* **No stubs in the boot path**: word 1 is a pure immediate change.
* `child-lock` prints its plan with `--dry-run`, reverts with `--restore`
  (three stock words plus zeroed RAM), and refuses to write when the three code
  sites hold neither the stock words nor this exact patch.
* A persistent variant for later (stubs inside the flashed image, canary-first
  rule, recovery) is written up in `tools/childlock_firmware_patch.md` - recipe
  only, nothing here builds or flashes an image.
* Known limitation: the UP arm is a one-shot byte with no timeout, so a UP tap
  followed by POWER *much* later still opens the menu.  Any other button, and
  the power-off/restore paths, either leave it alone or clear it.

---

## 8. Open questions / residual uncertainty

* Renderer→mode identity (state 1 "video", 2 "audio", 3 "playback", 4 "games",
  5 "settings") is **INFERRED** from the menu-index→mode mapping plus the
  legacy `tools/KC02Labels.java` names and the layer geometry. The *mapping* is
  CONFIRMED; the human labels are the inferred part.
* `key_mode3_down` (`0x02009568`) and the mode-3 sub-state bytes were not traced
  in detail.
* Event ids below `0x1a` are system events (USB `0x2`, timer `0xa`, …); their
  producers are spread across drivers and were only sampled.
* The auto-power-off timeout value comes from `ui_settings_get(8)`
  (`0x020064b4`); the id→field mapping of the settings getter is **INFERRED**.
* No claim here is backed by dynamic analysis: everything is static listing or
  raw image bytes. Hardware behaviour (especially the writability of the code
  region) is unverified — see the hoist's residual risks.
* The child lock's UP-handler identity (`0x02009890` for event `0x1b`) is
  CONFIRMED from the hook-list bytes, but its *behaviour* was not re-traced
  beyond the entry; nothing depends on what it does (the hijack re-executes the
  displaced instruction and returns into it).
* Whether a code-region poke actually reaches the instruction fetch path of a
  live core (I-cache vs. a copy-to-RAM at boot) is untested on hardware; the
  `child-lock` boot word and the hoist word share that risk (the hoist
  round-trip and the RAM-canary prove the write path, not the fetch path).
* The `ui_mode_pending = 3` write is transient by construction, and does nothing
  if the UI task is not running yet / if the camera booted into mode 10 because
  a host was already driving BOT.
