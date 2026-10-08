# SDK ↔ firmware map (AX3292 / KC02)

How much of `firmware/original.bin` can be identified from the leaked vendor SDK, and how that map was
produced. Machine-readable result: `/tmp/kc02-board-re/sdk_match_all.json` (per-object, per-function:
firmware file offset, CPU address, match score, sizes). Label TSV: `sdk_labels.tsv`.

## Method

Two independent sources:

1. **Vendor-compiled archives** `~/.src/ax3292-sdk/.../bwlib/*.a` — `libsd.a` (AX3292 SD driver),
   `libfs.a` (FatFs: `ff.o`, `diskio.o`, `link_ext.o`, `ffunicode.o`, `ffsystem.o`),
   `libavi.a` (AVI encoder/decoder), `libusbh.a` (USB host enumeration). Same vendor toolchain and ABI
   as the firmware, so these are byte-comparable.
2. **Locally compiled SDK objects** `/tmp/ax3292-toolchain/tests/sdk-probe/*.o` (165 objects) — the SDK
   HAL/app sources compiled with the LE-patched `or1k-elf-gcc` 4.9.1 (see the toolchain work). Unlinked
   objects, so branch/address immediates are still relocation placeholders — irrelevant for this
   matcher.

**Matcher** (`/tmp/kc02-board-re/sdk_match.py`): for every SDK function symbol (STT_FUNC with size),
take its instruction words and build a *shape signature* = `(opcode, rd, ra, rb)` of the first 5–12
instructions — i.e. **all immediate/offset fields are masked out**, which makes matching immune to
relinked addresses and unlinked relocations. Candidates are found by looking the signature up in a
prebuilt index of the firmware image (every 4-byte-aligned offset, file `0x200..0xD0000`), then
verified instruction-by-instruction under the same mask. A **function-end check** (the SDK function's
last instruction must match the firmware word at the same offset from the candidate start) suppresses
short-function false positives; short matches must additionally be exact.

Run it:

```bash
cd /tmp/kc02-board-re
python3 sdk_match.py /home/jbaiter/.src/ax3292-sdk/firmware0.96v2_totest/bwlib/*.a \
                    /tmp/ax3292-toolchain/tests/sdk-probe/*.o
# -> prints per-object coverage, writes sdk_match_all.json
```

Apply matches as Ghidra labels (creates missing functions first):
`ghidra_scripts/Kc02ApplySdkMap2.java` (reads `sdk_labels.tsv`, run through `analyzeHeadless -process … -noanalysis`).

## Result (current pass)

**278 functions matched across 123 SDK objects — 266 of them exact (≥99% shape fidelity).**
**239 unique labels applied to the Ghidra project** (89 functions created, 92 renamed, 147 already named).

| Area | Functions | Notes |
|---|---|---|
| HAL drivers | 116 | `src_ax32xx_isp` 12, `ax32xx_gpio` 10, `osd_ax32xx_osd` 10, `mjpeg_ax32xx_mjpeg` 7, `src_ax32xx_misc` 7, `src_hal_rtc` 6, `src_hal_mjpegB` 5, `src_hal_stream` 5, `ax32xx_sys` 4, `lcd_ax32xx_lcd` 4, `src_ax32xx_iic` 3, `src_ax32xx_rtc` 3, `src_ax32xx_tminf` 3, `csi_ax32xx_csi` 3, `src_hal_auadc` 3, `src_hal_mjpeg` 3 … |
| FatFs | 37 | `create_chain` → CPU `0x0206E5AC`, `f_rename` → `0x02072F14`, `dir_find` → `0x0206FF28`, `f_sync`, `f_unlink`, `f_chmod`, `f_utime`, `clst2sect` … |
| AVI video | 19 | `avi_odmlService` → `0x0206A180`, `avi_odmlAddIndex`, `avi_standardAddIndex`, `avi_standardDecodeFrame`, `aviDecodeIdx1*` |
| SD card | 19 | `ax32xx_sd0Recv` → `0x02068800`, `ax32xx_sd0Send`, `ax32xx_sdCRCCheck`, `src_hal_sd` |
| sUI (UI framework) | 19 | `widgetGetId`/`widgetSetId`/`widgetGetType`, `itemCreateMenuItem*`, `winManage`, `itemManage` |
| USB | 16 | `husb_pidin_get` → `0x0204E8F4`, `husb_pidin_next`, `husb_pidout`, `husb11_pidin_get`, `hal_usb_msc/uac/uvc` |
| RTOS (`xos`) | 11 | `XMsgQCreate/Post/Pend/Flush/PostFront` → `0x02021540`/`0x02021648`/`0x0202173C`…, `XMboxPost/Pend`, `XWorkCreate/Destory` |
| Sensor | 9 | `cmos_sensor.c` / some `SENSOR_*.c` fragments only — see below |
| Other / app | 32 | `std_*`, `res_*` (font/icon resource helpers), `hal_watermark`, `Maxlib` etc. |

Cross-validation: `XMsgQPost → 0x02021648` matches the queue post address identified independently from
disassembly in the keypad analysis (`board-keypad-ghidra.md`), and `ff.o`/`ax32xx_sd.o` land exactly in
the storage region the firmware uses for `res/lib/sensor.bin` loading — the map is self-consistent.

## What is *not* in the SDK map (and why it matters)

* **The sensor layer is custom.** The SDK's `device/cmos_sensor/SENSOR_*.c` drivers (H62/H65/GC*/OV*)
  matched ~9 fragments only, while the firmware's sensor bring-up is built around a **resource item**
  model (header at `0x020CB3B0`, H63P item at `0x020CB3F0`, init table at `0x020D09B0`) that does not
  exist in the SDK. The KC02 sensor path must be written/ported rather than reused — see
  `board-sensor.md`.
* **The printer has no SDK counterpart at all** (the SDK is a dashcam/car SDK). The whole mechanism +
  head protocol is reverse engineered — see `board-printer.md`. This is new code for any custom build.
* **The KC02 application layer** (kids UI, print tasks, photo frames, games) is app-specific: only ~32
  generic helpers matched. The app is where the custom firmware will differ anyway.
* Unmatched SDK objects are mostly *unreferenced* library variants (e.g. `SENSOR_*` models the KC02 does
  not use, `tft_lcd_*` panels not fitted, `gsensor_*` — the SELFTEST reports the G-sensor as NOT
  populated).

## Extending the map

1. The SDK probe currently compiles 126/239 translation units; the failures are 74 Windows
   include-case mismatches and 39 `__sfr` uses (see `~/.src/ax3292-toolchain/README.md`). Fixing those
   produces objects for every HAL/app file, and re-running the matcher labels the remainder
   mechanically — no manual RE needed.
2. The vendor linker map `ax32_platform_demo/output/ax329x_sdk.map` (2561 symbol addresses for the
   0.96″ variant build) is a second reference: it gives the SDK's function order and sizes, useful for
   aligning regions where byte-matching fails (e.g. because of flag/version drift).
3. `full.dis` (`~/.src/ax3292-toolchain` build tree) is the whole-image disassembly to search when a
   symbol falls outside the app image (`app.dis` stops at file `0xC8800`).

## Reusable artifacts

| Artifact | Purpose |
|---|---|
| `/tmp/kc02-board-re/sdk_match.py` | the matcher (relocation-tolerant shape matching) |
| `/tmp/kc02-board-re/sdk_match_all.json` | full map: SDK object/function → firmware CPU address + score |
| `/tmp/kc02-board-re/sdk_labels.tsv` | de-duplicated label list for Ghidra |
| `ghidra_scripts/Kc02ApplySdkMap2.java` | applies the TSV (creates functions, renames, comments) |
| `ghidra_scripts/Kc02FinalNames.java` | the hand-verified board names (printer/keypad/sensor/UI) |
