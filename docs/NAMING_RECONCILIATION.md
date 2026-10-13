# Naming reconciliation

The same firmware functions carry up to three different names across the repository:

1. **Ghidra project names** - what a user sees in Ghidra, and what
   [`ghidra_symbols.tsv`](../tools/ghidra_symbols.tsv) replays. Some are taken from the vendor SDK.
2. **Screen reconstructions** - `camera_screen/entrypoints.tsv` and `main_menu/entrypoints.tsv`,
   the names the rebuilt modules use.
3. **[UI_STATE_MACHINE.md](UI_STATE_MACHINE.md)** - hand labels written early (`ui_frame_hook`,
   `key_hook_install_primary`, `key_modeN_*`).

An audit against the firmware tables found 26 disagreements. Most are stylistic; a handful are
**provably wrong**, and those are what this document settles.

## Rules

Applied in this order, so a name is chosen by evidence rather than by preference:

1. **Key handlers are named after the key they serve**, because a window keymap plus the ADC key
   table proves the binding. Canonical form: `<screen>_key_<BUTTON>` or the reconstruction's
   existing `<screen>_<button>`. A name that contradicts the keymap is wrong, not a matter of taste.
2. **Where the vendor SDK defines the function, the SDK name wins** - it is the vendor's own name for
   their own code.
3. **Otherwise the existing project name stays**, with the other two vocabularies recorded as
   aliases so a reader arriving from any document can find the code.

Physical key ids, from the ADC threshold table at `0x020C2680` (UI_STATE_MACHINE §4.1):

| key id | button | adc centre |
|---|---|---|
| `0x1a` | OK | 386 |
| `0x1b` | UP | 657 |
| `0x1c` | DOWN | 510 |
| `0x1d` | LEFT | 252 |
| `0x1e` | RIGHT | 776 |
| `0x24` | POWER | 12 |

Note `0x24` is the **POWER** key, not a back key. Several documents call it "back" because the
camera-mode handler for it requests the main menu; the key itself is POWER.

## Corrections applied (provable)

These project names contradicted the keymap. Evidence: the camera keymap at `0x020C2B50` and the
main-menu keymap at `0x020C4C3C`, both read from the image.

| Address | Keymap says | Project had | Now | Reconstruction calls it |
|---|---|---|---|---|
| `0x02009568` | `0x1C` DOWN | `video_key_prev` | **`key_mode3_down`** | `camera_down` |
| `0x02009890` | `0x1B` UP | `video_key_next` | **`key_mode3_up_handler`** | `camera_up` |
| `0x02009DE0` | `0x1D` LEFT | *(unnamed)* | **`key_mode3_left`** | `camera_left` |
| `0x02009F6C` | `0x1E` RIGHT | `ui_key_cycle_state` | **`key_mode3_right`** | `camera_right` |
| `0x02013828` | `0x1C` DOWN | `main_menu_page_up` | **`key_mode6_down`** | `main_menu_down` |
| `0x020138B0` | `0x1B` UP | `main_menu_page_down` | **`key_mode6_up`** | `main_menu_up` |

Two things are worth recording about how this happened.

**The two main-menu cases were swapped**, not merely misnamed: the DOWN handler carried the UP name
and vice versa. Both handlers perform the same row swap, which is why no test caught it.

**The names came back from `ghidra_symbols.tsv`, not from a new invention.** That manifest already
held the correct key-based names - `key_mode3_down` and friends, each with a `CONFIRMED` dispatch
note - and the UI document uses the same vocabulary. The project had drifted away from it because a
later behavioural naming pass (`docs/apply_names.tsv`, `apply_final.tsv`) overwrote them with names
describing what the handlers *do* (`video_key_prev`, `ui_key_cycle_state`). Both descriptions were
accurate; only the binding is provable, so the manifest's names win and the behavioural text is kept
in the comment.

The `video_*` prefix additionally came from an early pass that treated mode 3 as "video". Mode 3 is
the photo camera - its lifecycle functions are `mode_photo_*` - so that prefix was wrong twice over.

**A caution for future renames:** `Function.setComment()` replaces the plate comment outright. The
first attempt at this rename used it and destroyed the `CONFIRMED` evidence notes on all six
functions. They were restored from the manifest and the comments now carry both the evidence and the
reconciliation note. Renames should set the name, then append to the existing comment.

## Aliases kept (stylistic, not wrong)

Both names are defensible here, so the project name stands and the others are aliases.

| Address | Canonical | Aliases | Note |
|---|---|---|---|
| `0x02000404` | `app_main_loop` | `ui_frame_hook` | UI_STATE_MACHINE's name is arguably **more precise**: the function has exactly one caller (the mode walker) and runs once per frame. It is not a loop. |
| `0x02004944` | `ui_state_apply` | `ui_render_dispatch` | applies the UI-state byte to the renderer |
| `0x020079B4` | `ui_key_dispatch` | `key_event_dispatch` | |
| `0x02007DA8` | `ui_mode_timeout_tick` | `key_event_idle_timeout` | |
| `0x02007EE4` | `ui_leave_mode` | `key_event_usb_connect` | |
| `0x02008188` | `mode_manager_loop` | `ui_mode_machine_walker` | |
| `0x020084A0` | `mode_photo_tick` | `ui_mode3_photo_frame` | |
| `0x02009E70` | *(unnamed)* | `key_mode3_event01_overlay` | handles message `0x01`, worth naming |
| `0x0200A4BC` | `ui_task_print_image` | `camera_ok`, `key_mode3_ok` | it is the OK handler **and** the print task |
| `0x02009F08` | `kc02_power_handler` | `camera_back`, `key_mode3_power_request_menu` | the POWER key (`0x24`); its effect is to request the main menu, which is why "back" was used |
| `0x02013714` | `main_menu_next_item` | `main_menu_right`, `key_mode6_right` | |
| `0x0201379C` | `main_menu_prev_item` | `main_menu_left`, `key_mode6_left` | |
| `0x02013938` | `main_menu_ok` | `key_mode6_ok_select` | |
| `0x02013A14` | `video_request_present_mode` | `main_menu_playback_shortcut`, `key_mode6_event1f_playback` | handles message `0x1f` |

## SDK-backed names (rule 2)

Verified present in the related vendor SDK, so these are the vendor's own names and the hand labels
are the aliases.

| Address | SDK name | Documented elsewhere as | SDK source |
|---|---|---|---|
| `0x02007880` | `msgFuncRegister` | `key_hook_install_primary` | `ax32_platform_demo/application.h`, `msgDeal.c` |
| `0x02007938` | `unshieldedMsgFuncRegister` | `key_hook_install_system` | `ax32_platform_demo/application.h`, `main.c` |
| `0x0200411C` | `dispLayerPIPOnlyFront` | `ui_render_state_0` | `ax32_platform_demo/display.c` |
| `0x020250A4` | `itemManageCreateIndirect` | widget constructor | `sUI/itemManage.h` |
| `0x020253C8` | `frameWinCreateIndirect` | root constructor | `sUI/frameWin.h` |
| `0x0202566C` | `imageIconCreateIndirect` | image constructor | `sUI/imageIcon.h` |
| `0x0202808C` | `rectCreateIndirect` | rect constructor | `sUI/rect.h` |
| `0x020283CC` | `stringIconCreateIndirect` | string constructor | `sUI/stringIcon.h` |

**`win_open` at `0x02012304` is not an SDK name.** `sUI/winManage.h` has `winCreate`, `winDestroy`,
`winSendMsg`, `winSetVisible`, `winSetStrInfor` and friends, but nothing named `win_open`, and the
function here takes a window descriptor rather than explicit geometry. The UI document's
`ui_object_start` is not an SDK name either. Both are descriptive; the project name `win_open` stands
by rule 3, but neither should be quoted as vendor API.

## Known remaining drift

* **Raw exports keep their export-time names.** `docs/main_menu/main_menu_screen_decompiled.c` and
  `docs/camera_screen/camera_screen_decompiled.c` are verbatim Ghidra output, so they still contain
  `main_menu_page_up` / `main_menu_page_down`. That is the state at export time and is left alone as
  evidence; the reconciled names are in the table above.
* **`tools/ghidra_symbols.tsv` has not been re-listed for the corrections** - it did not need to be,
  because it was already correct for these six. It is now the authority the project agrees with.
* **My module symbol names** (`camera_down`, `main_menu_down`, …) differ from the canonical
  `key_mode*` names. They are the symbols inside the built modules and are pinned by
  `build/manifest.json` and `test_results.json`; renaming them would invalidate those hashes for no
  behavioural gain, so they stay as documented aliases.

## How this was applied

Renames were applied to the **original** program additively, with user-defined names, and every
comment was made to preserve the manifest's evidence text. Nothing here changes code, addresses or
behaviour - only the names recorded against them.

Because the newer builds get their names by propagation from the original, these corrections reach
them on the next propagation pass rather than being hand-edited.
