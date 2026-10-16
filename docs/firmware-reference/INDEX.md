# KC02 subsystem reference index

Where the clean source for each part of the camera lives. Two tiers:

- **SDK** — the vendor ships readable C for the driver. Read that; do not
  re-decompile it and do not transcribe it into this directory. Paths are under
  `~/ax3292-sdk/firmware0.96v2_totest/`.
- **FIRMWARE** — application logic that exists only as compiled code in
  `firmware/original.bin`. That is what the `*.c` files here are for.

The rule that follows from this: **check the SDK first.** Almost every "we need to
reverse this" moment in this project turned out to be either an SDK function being
called wrongly or a vendor driver whose call sequence we had guessed.

## Display and LCD

| need | tier | location |
|---|---|---|
| LCD controller init, kick, panel config | SDK | `ax32xx/lcd/ax32xx_lcd.c`, `hal/src/hal_lcd.c`, `hal/src/hal_lcdframe.c` |
| DE video/OSD layers, `videoSetAddr`, `deUpdate` | SDK | `ax32xx/osd/ax32xx_osd.c` |
| OSD compositor / alpha | SDK | `ax32xx/osd/`, `ax32xx/src/ax32xx_osdcmp.c`, `hal/src/hal_osd.c` |
| LCD frame kick = `LCD_CON |= 2` | FIRMWARE | `display.c` (wrapper at `0x02035814`) |
| DE register contract as measured | FIRMWARE | `display.c` |

## Camera: sensor, CSI, rotation

| need | tier | location |
|---|---|---|
| Sensor bus (bit-banged I2C), autodetect, init, exposure | FIRMWARE | `sensor_iic.c` |
| CSI type/mode/enable, LCD plane, scaler, DMA, **frame interrupt** | FIRMWARE | `csi.c` |
| CSI/ISP HAL layer | SDK | `hal/src/hal_csi.c`, `hal/src/hal_usensor.c`, `ax32xx/src/ax32xx_isp.c` |
| MJPEG capture | SDK | `hal/src/hal_mjpeg.c`, `hal/src/hal_mjpegB.c`, `hal/src/hal_stream.c` |
| Rotation engine | FIRMWARE + SDK | `rotate_pip.c` and `ax32xx/src/ax32xx_rotate.c`, `hal/src/hal_rotate.c` |
| Sensor register tables | FIRMWARE | `../table/h63p-init.json` (extracted, 100 pairs) |

## Power, clocks, battery

| need | tier | location |
|---|---|---|
| PMU: battery, charging, power-off | SDK | `ax32xx/src/ax32xx_pmu.c`, `hal/src/hal_pmu.c` |
| RTC, low-power wake | SDK | `ax32xx/src/ax32xx_rtc.c`, `hal/src/hal_rtc.c` |
| LDO rails, **module clock gates**, pin muxing | FIRMWARE | `power_clock_gpio.c` |
| Auto power-off timing | FIRMWARE | see `docs/RAM_MAP_MEASURED.md` and the 300 s measurement in `docs/hw/2026-10-15.md` |

## Input

| need | tier | location |
|---|---|---|
| ADC (the button ladder) | FIRMWARE | `adc_keypad.c` |
| ADC driver | SDK | `ax32xx/src/ax32xx_adc.c`, `hal/src/hal_adc.c`, `ax32xx/src/ax32xx_auadc.c` |
| GPIO, external interrupt | SDK | `ax32xx/src/ax32xx_gpio.c`, `hal/src/hal_gpio.c`, `hal/src/hal_int.c` |
| Key ladder windows as measured | FIRMWARE | `../fixtures` -> `tests/fixtures/hardware/original-2026-10-15.json` |

## Storage

| need | tier | location |
|---|---|---|
| SD controller | SDK | `ax32xx/inc/ax32xx_sd.h`, `hal/src/hal_sd.c` |
| SPI / NOR flash | SDK | `ax32xx/src/ax32xx_spi.c`, `hal/src/hal_spi.c` |
| DMA engine | SDK | `ax32xx/src/ax32xx_dma.c` |
| FAT and card bring-up as Luma has it | LUMA | `custom-firmware/src/storage.c`, `sd_hw.c` |

## USB

| need | tier | location |
|---|---|---|
| USB device controller driver | SDK | `ax32xx/src/ax32xx_usb.c`, `ax32xx/inc/ax32xx_usb.h` |
| USB device task / descriptors | SDK | `ax32_platform_demo/taskUsbDevice.c` |
| Bulk-only transport over the vendor opcode | FIRMWARE | host side is `tools/dump_firmware.py`; the device side is in `ax32xx_usb.c` |

## Printer

| need | tier | location |
|---|---|---|
| Thermal head job: pin setup, head power, strobe ramp, feed | FIRMWARE | `printer.c` |
| Thermal printer HAL wrapper (`hal/tminf`) | SDK | `ax32xx/src/ax32xx_tminf.c` |
| Luma's printer integration point | LUMA | `custom-firmware/src/paint.c`, and the `print_photo` slot of `Platform` (currently NULL) |

## Application logic that has no SDK equivalent

These are the parts that must be read from `firmware/original.bin`:

- camera-entry sequencing: what runs *around* `sensor_init` to actually start and
  service a capture stream, and the CSI interrupt callbacks it registers
- the UI/mode state machine and power-off countdowns
- printer job orchestration
- the every-mode frame hook at `0x02000404` used by the RAM hoist

## How to re-check anything here

```sh
# Ghidra headless via the pi-ghidra tool, cache is already warm:
#   ghidra action=decompile address=ram:020583f0 binary=firmware/original.bin
#
# SDK cross-references: the file name in the table above is the SDK source.
# Firmware strings and tables: docs/hw/table/, tests/fixtures/hardware/
```

## The complete address map

Do **not** duplicate the address map into this directory. The repo already keeps a
curated, replayable annotation manifest:

    tools/ghidra_symbols.tsv      <addr> TAB <kind> TAB <name> TAB <comment>

It covers the UI state machine and renderers, the mode machine and its twelve mode
records, the per-mode key handlers, settings, the key ADC, the USB/BOT service
path, the flash writers (explicitly suffixed `_DANGEROUS_`) and the cache helper.
`tools/ghidra_symbols.py` replays it onto a Ghidra cache.

So the division is: **the manifest is the map, these files are the bodies.**

## Transcribed so far

| file | subsystem | state |
|---|---|---|
| `sensor_iic.c` | sensor bus, autodetect, init, exposure | complete for the path Luma uses |
| `csi.c` | CSI type/mode/enable, LCD plane, scaler, DMA, **frame interrupt** | complete |
| `display.c` | LCD kick, frame events, measured DE contract | complete |
| `power_clock_gpio.c` | LDO, module clock gates, GPIO and pin mux | complete |
| `adc_keypad.c` | ADC sequence and measured ladder windows | complete |
| `rotate_pip.c` | rotation engine, pip counter | complete |
| `printer.c` | thermal printer, end to end | **complete.** 15 of 18 bodies written out; `kc02_print_layout`, `kc02_load_print_image` and `kc02_print_image` are summaries; `kc02_print_file` + the 2 UI tasks not yet read |
| `usb_bot.c` | BOT/MSC service and the manifest discrepancy | service path complete |
| `isp_csi_registers.md` | **the SDK's own name and bit meaning for all 124 registers in 0xb000-0xb1E8**, plus the measured working values | complete, SDK-sourced |

## Still to transcribe (the work list)

Each of these is identified by address in the manifest; only the body is missing.

- **printer**, remaining: full bodies for `kc02_print_layout` 0x0205ab70,
  `kc02_load_print_image` 0x020593cc and `kc02_print_image` 0x0205b0fc (currently
  summaries), plus `kc02_print_file` 0x0205adf0, `ui_task_print_image`
  0x0200a4bc and `ui_task_print_file` 0x0200c24c, which have not been read
- **UI / mode machine**: `ui_render_dispatch` 0x02004944, the six
  `ui_render_state_N` renderers, `ui_mode_machine_walker` 0x02008188,
  `ui_mode_request` 0x02008108, the mode enter/frame/exit triples per mode
- **keys**: `key_adc_init` 0x02002e0c, `key_adc_handler` 0x02003a60,
  `key_event_dispatch` 0x020079b4 and the hook tables
- **power**: `sys_key_activity_autopoweroff` 0x0200cce8,
  `key_event_idle_timeout` 0x02007da8, `kc02_sys_shutdown` 0x0204c9fc,
  `kc02_battery_check` 0x02059290, `kc02_power_handler` 0x02009f08
- **flash, read-only reference only**: `spi_read` 0x0203d38c and the manifest's
  `_DANGEROUS_` erase/program entries, kept so the write path is *understood* and
  deliberately not called
- **SD**: `hal_sd` is in the SDK; the firmware-side glue still needs a body

## A caution this exercise produced

Cross-checking the manifest against Ghidra immediately found a mismatch:
`0x02021648` is annotated `usb_reenumerate` but decompiles to `XMsgQPost`, a
message-queue post. Recorded in `usb_bot.c`. Treat a manifest entry as a hint until
its body has been read.

## What the printer work turned up

Three findings worth keeping, all from reading the code rather than guessing:

1. **The head shares the sensor's IIC pins.** Data is PORTE.15 and clock is
   PORTE.13 -- the same lines as software-IIC SDA/SCL. That is why every job
   starts by driving them low, and why a print cannot overlap a sensor read.

2. **The platen is not stepped separately during a print.** The four coil phases
   asserted by `kc02_print_head_ramp` at sub-strobes 0/8/16/24 are verbatim
   `kc02_motor_step`'s phase sets A and B (checked line by line). The motor and
   head advance in lockstep; `kc02_motor_step` is the standalone-feed entry.

3. **The head's buffer is transposed.** It is indexed
   `canvas[col * stride + row]`, row varying fastest, which is why
   `kc02_print_layout` exists as a separate reordering step. A port feeding a
   row-major buffer prints a rotated image that still looks plausible because the
   geometry is near-square.

Two unexplained observations were recorded rather than smoothed over: battery code
7 is never returned by `kc02_battery_check` yet has a live case in
`kc02_head_strobe_delay`, and the run_B and head_ramp energy bases move in opposite
directions with charge.
