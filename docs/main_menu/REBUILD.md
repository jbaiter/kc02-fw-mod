# Rebuild the main-menu controller

From the repository root:

```sh
python3 docs/main_menu/build_main_menu.py \
  --firmware firmware/original.bin \
  --tool-prefix /tmp/ax3292-toolchain/prefix/bin/or1k-elf- \
  --base 0x02310000 \
  --out /tmp/kc02-main-menu-build

python3 docs/main_menu/test_main_menu.py \
  --firmware firmware/original.bin \
  --out /tmp/kc02-main-menu-tests
```

The build uses GCC 4.9.1, `-mno-delay -mhard-div -Os`, freestanding C11 and
warnings as errors. It checks the image hash, target ELF format, all 15 exported
menu entries, the complete 384-byte widget table and all 11 relocated handlers.
No undefined symbols or BSS remain.

The saved [`build/main_menu.elf`](build/main_menu.elf) and
[`build/main_menu.bin`](build/main_menu.bin) contain a **2,800-byte module**:
2,356 bytes of code/constants, 400 bytes of writable data and 44 alignment bytes.
[`build/manifest.json`](build/manifest.json) records source/binary hashes,
imports, replacement symbols and unapplied hook candidates.

**The example link address is not reserved or verified free RAM.** This is not
a `DestBin.bin` update image or a USB callback payload. The entry point is not
necessarily the binary's first byte. Neither script installs hooks, patches an
image, accesses USB or flashes the camera. Compilation does not establish
on-device ABI, memory ownership or image-load placement.

The controller retains stock framework/drivers and resources. Its creation
records use 48-byte firmware layouts, not the related SDK's 40-byte struct.
The generator preserves padding and unknown extension bytes and emits readable
designated initializers in `main_menu_widgets.inc`.

## Tests and fidelity limits

The host-mock suite passes **1,101 assertions** with the undefined-behavior
sanitizer. It checks navigation/flag guards, six mode routes, the audio
transition, renderer argument contracts/coordinates, label visibility, blink
thresholds and allocation/free behavior. It covers all eight allocation-failure
points. See [`test_results.json`](test_results.json).

The test stubs do not execute the stock JPEG/BMP decoders, compositor or LCD
engine. These are controller tests, not whole-program equivalence or hardware
validation. The readable C uses helpers/loops where the stock binary unrolls
operations; do not expect identical instruction bytes.

The reconstruction retains the stock allocation-failure behavior: a failed icon
allocation leaks the temporary 27,648-byte BMP scratch buffer. The window exit
frees the background and known icon buffers but cannot recover that scratch
pointer. Initializers also ignore decode errors, and the renderer trusts a valid
selected index and initialized dimensions. These need review before deployment.

## Editing the screen

- Caption placement/resources: generated widget records and `tables.json`.
- Mode routes: `main_menu_ok` in `main_menu_screen_model.c`.
- Grid coordinates and selection resources: `icon_positions` and `icon_resources`.
- Blink handling: `main_menu_blink`; stock message delivery also controls cadence.
- Normal icon appearance: [`assets/background.jpg`](assets/background.jpg).
- Selected appearance: the six BMPs under [`assets/`](assets/).

**Editing one selection BMP does not change the normal icon baked into the JPEG.**
The current icon width/height are both 96. Audit the 12-argument blitter and the
stock height/width ordering before making tiles rectangular.

Candidate mode-6 callback slots are `0x020C4C20`, `0x020C4C24`, `0x020C4C28`
(enter, exit, tick). First reserve loadable memory, verify the runtime ABI and
lifecycle, and prepare cache maintenance/rollback from a non-menu mode. Keep the
stock scheduler, message dispatch and window-exit cleanup. Repair power and
validate recovery before persistent flashing.
