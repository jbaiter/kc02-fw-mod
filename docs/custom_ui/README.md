# Custom UI routing patch

A custom firmware image with this behaviour:

| Action | Result |
|---|---|
| boot | camera screen (mode 3) instead of the main menu |
| UP, then POWER within 1 s | settings (mode 9) |
| DOWN, then POWER within 1 s | photo viewer (mode 4) |
| POWER in settings or the viewer | back to the camera screen (mode 3) |

Build the patch into an image:

```sh
python3 docs/custom_ui/build_ui_router.py --out /tmp/kc02-custom-ui-build
python3 docs/custom_ui/patch_custom_ui.py --dry-run
python3 docs/custom_ui/patch_custom_ui.py -o patched/custom_ui/custom_ui.bin
```

## Sequence, not chord

The two-key gestures are **two taps, in order**, with the second arriving inside one second. The
panel is an ADC resistor ladder on a single analogue pin and resolves one key at a time, so two
simultaneous presses cannot be told apart - a chord was never available here. The existing child-lock
patch works the same way for the same reason.

The window is measured against the cached tick at `0x020CCDE8`, which `system_tick` refreshes roughly
every 10 ms. That is why the timing is ~10 ms accurate and why no `XOSTimeGet` call appears in the
patched code: the call would have needed a stack frame in both the router and the arming block, and
neither slot had room for one.

## Why a router and not a replacement UI loop

The stock mode machine, message dispatcher, scheduler, preview pipeline and drivers are all kept.
They are the parts that are proven on hardware, they carry USB and the flash-dump path, and they are
large. The requested flow is a routing question between three stock modes, so it is implemented as
213 bytes of routing rather than a replacement loop.

If genuinely custom *screens* are wanted later, that is the separate `camera_screen` module
(10,768 bytes), which needs a verified free region to live in. This patch deliberately avoids that
problem.

## How it fits in the image

Nothing is added to the image and no assumed-free region is used. Each piece is built into a slot
that already exists, where the stock handler it replaces lived, so the application image keeps its
original extent:

| Slot | Budget | Used | Replaces | Contains |
|---|---|---|---|---|
| `0x02009F08` | 100 B | 88 B | camera POWER handler | `camera_power_router` |
| `0x02016C14` | 100 B | 80 B | settings POWER handler | `camera_arm_up`, `camera_arm_down` |
| `0x0200BD4C` | 100 B | 68 B | playback POWER handler | `power_to_camera` |

`build_ui_router.py` asserts each slot against its budget, and `patch_custom_ui.py` refuses to run if
any stock word it expects to patch is not what it expected.

The zero-filled runs elsewhere in the image were deliberately **not** used. The largest of them,
5672 bytes at `0x0207C640`, is the print-style LUT that [CHILD_MODE.md](../CHILD_MODE.md) already
records as a trap ("the old persistent recipe incorrectly proposed that LUT as free stub space");
the rest are unverified and could be zero-at-build-time data tables.

### Byte-level changes

Seven spans, 213 bytes of a 4 MiB image:

| CPU | File | Change |
|---|---|---|
| `0x020004AC` | `0x002AAC` | boot initial non-USB mode, `6` -> `3` |
| `0x02009F08` | `0x00C508` | 88 B router into the camera POWER slot |
| `0x0200BD4C` | `0x00E34C` | 68 B `power_to_camera` into the playback POWER slot |
| `0x02016C14` | `0x019214` | 80 B arming block into the settings POWER slot |
| `0x020C2B6C` | `0x0C516C` | camera keymap msg `0x1B` (UP) -> `camera_arm_up` |
| `0x020C2B74` | `0x0C5174` | camera keymap msg `0x1C` (DOWN) -> `camera_arm_down` |
| `0x020C5470` | `0x0C7A70` | settings keymap msg `0x24` (POWER) -> `power_to_camera` |

The playback keymap entry for POWER needs no change: `power_to_camera` is built where that entry
already pointed.

Every one of the three slots was checked to be referenced by exactly one keymap entry - the entry
this patch rewrites - so no other mode sees the replaced bodies. The header checksum covers
`0x00..0x1FF` only and none of these spans touch it, so no checksum fix is needed.

## Known behaviour changes

* **No transition animation** when POWER returns to the camera from settings or the viewer. The stock
  handlers armed the transition bytes at `app_context+0x68/+0x69` before requesting a mode; the
  replacements do not. The mode switch itself is unchanged.
* **POWER in camera mode does nothing** unless a sequence is armed. Stock behaviour was to open the
  main menu. The main menu is therefore reachable only through the sequences above.
* The arming block writes two RAM bytes at `0x020D9480`/`0x020D9484`. That page is the same scratch
  area the child-lock patch uses, so this patch and the child lock are alternatives, not companions;
  applying both would have them fight over the same bytes. Both are cleared at power-on.
* UP and DOWN keep their stock behaviour - each arming wrapper hands over to the stock handler, which
  still runs from its own entry point.

## Not verified, and the risk that matters

**This image has never been run.** It has passed offline checks only:

* the source image hash is pinned and checked before patching;
* every changed byte falls inside a planned span and every span changed;
* each slot is inside its 100-byte budget;
* each replaced slot is referenced by exactly one keymap entry;
* the patched keymap entries and boot word read back as intended.

None of that says the camera boots. The install step is the dangerous one, and
[CHILD_MODE.md](../CHILD_MODE.md) states the position for this whole class of change: it "has passed
offline checks, not a hardware installation test. Keep your working backup and an external SPI
recovery method before the first installation. This does not guarantee recovery from a bad flash."

The repository still has **no verified update/recovery cycle**, which is the standing blocker in
[NEXT_STEPS.md](../NEXT_STEPS.md). Before this image goes near the camera:

1. **Validate the stock updater with an unchanged image first.** Write the *unmodified* dump as
   `DestBin.bin` and let the camera update itself. That proves the update path and the recovery path
   without risking a modified image, and it is the cheapest insurance available.
2. **Verify an external SPI read works** on this unit - clip, read, compare against the known dump -
   so that a bad flash is recoverable rather than terminal.
3. Only then install the patched image, and expect to have to recover at least once.

## Files

| File | Purpose |
|---|---|
| `ui_router.c` | the patch: router, arming block, `power_to_camera` |
| `ui_router.ld` | pins each piece into its slot and asserts the 100-byte budget |
| `build_ui_router.py` | compiles, links, extracts the slot blobs, checks the budget |
| `patch_custom_ui.py` | applies the spans to an image, with `--dry-run` and span verification |
