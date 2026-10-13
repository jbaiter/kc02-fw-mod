# KC02 board / hardware reverse engineering

Static analysis of `firmware/original.bin` (AppoTech/Buildwin **AX3292**: little-endian OpenRISC
ORBIS32, no delay slots), cross-checked against the leaked vendor SDK
(`~/.src/ax3292-sdk`, `AX329x_car_sdk_release_v102`) and its register map (`ax32xx/inc/ax329x.h`).

Detailed per-subsystem reports (produced in a parallel RE pass, each with instruction-level evidence):

| Report | Contents |
|---|---|
| `~/.src/ax3292-toolchain/docs/board-printer.md` | printer module map, port bits, head drivers, print API, status codes |
| `~/.src/ax3292-toolchain/docs/board-sensor.md` | H63P descriptor/item, init table (100 pairs), I²C/CSI chain, SDK comparison |
| `~/.src/ax3292-toolchain/docs/board-keypad-ghidra.md` | keypad scan/debounce/dispatch, hook tables, Ghidra `spr` fidelity fix |

## Address mapping

```
CPU = file_offset + 0x01FFDA00        file_offset = CPU - 0x01FFDA00
```

The app image is copied from flash into SDRAM at `0x02000000` (`ax329x.ld` VMA/LMA split), so code and
its RAM variables share one linear CPU space. Verified landmarks:

| file | CPU | meaning |
|---|---|---|
| `0x002AAC` | `0x020004AC` | boot `l.addi r3,r0,6; l.addi r4,r0,0; l.jal ui_mode_request` |
| `0x00C508` | `0x02009F08` | POWER key handler |
| `0x0D2FB0` | `0x020D09B0` | H63P sensor init table (in flash, past the app image) |

**Disassembly coverage matters:** the app image spans file `0x2600..0xC8800` (CPU `0x02000000..0x020C6200`).
The sensor resource block (CPU `0x020CB3B0+`) and the ISP calibration tables live **past that cut** and
are only visible in a full-image listing:

```
or1k-elf-objdump -D -b binary -m or1k --adjust-vma=0x01FFDA00 original.bin > full.dis   # whole image
or1k-elf-objdump -D -b binary -m or1k --adjust-vma=0x02000000 app.bin     > app.dis     # app only
```

`FIRMWARE_NOTES.md` writes function addresses as `CPU - 0x02000000` (e.g. "key scan 0x004CC8" = CPU
`0x02004CC8`); table/asset addresses there are **file offsets**.

## Keypad (ADC resistor ladder)

**Window table**: CPU `0x020C268C` (file `0x0C4C8C`), 6 records × 8 bytes,
`[u16 key_id][u16 center][u16 lo][u16 hi]`, ending exactly at the ops vtable at `0x020C26BC`.

| key_id | button | center (flash) | window (derived at runtime) |
|---|---|---|---|
| `0x1A` (26) | OK | 386 | 351–421 |
| `0x1E` (30) | RIGHT | 776 | 741–811 |
| `0x1D` (29) | LEFT | 252 | 217–287 |
| `0x1B` (27) | UP | 657 | 622–692 |
| `0x1C` (28) | DOWN | 510 | 475–545 |
| `0x24` (36) | POWER | 12 | 0–47 (low clamps to 0) |

`kc02_adc_init` @ `0x02002E0C` fills `lo = center-0x23`, `hi = min(center+0x23, 0x400)`, then writes
`0x020C87D8 = 3` and enables IRQ `(2, 0x2000)`. ADC channel 3 is the keypad input (channel 2 is armed
to trigger the conversion).

**Pipeline** (all verified):

| stage | address | behaviour |
|---|---|---|
| poll task | `0x0200CC20` | ~10 ms tick: `dev_ioctl(ad-key, 9, &evt)` → posts `evt` to the UI queue at `*(0x020D416C+116)` |
| scan + debounce | `0x020035F8` | read ADC ch3, match windows (last match wins), require 3 identical samples |
| press/release + long-press | `0x020036D8` | per-key hold counters; POWER released <30 polls → `0x24`, held ≥30 → `0x26` (boot-level handler `0x02007E20`); RIGHT long → `0x2C`; OK long → remap; LEFT gated by fast-repeat flag `0x020C8780` |
| event word | `0x02003990` | `key_id | 0x10000` (auto-repeat: 10 polls, 5 when fast flag set) `| 0x20000` (release) |
| UI dispatcher | `0x020079B4` | pops the queue, `key = ev & 0xffff` (<0x2E), calls primary/secondary hooks |
| hook tables | `0x020CCC8C` (mode) / `0x020CCBD4` (global) | 46 entries each, filled by `0x02007880` / `0x02007938` from `{key_id, handler}` lists `0x020C2A4C` (built-in), `0x020C2A64` (boot, from `0x020004C0`), `0x020C2B50` (menu mode, via descriptor `0x020C2B40`) |

Dispatcher ABI (matches `CHILD_MODE.md`): `handler(r3 = UI context, r4 = 1, r5 = &flags)`, return to
`0x02007A9C`, called through `r14`; the `l.jalr r14` sits at `0x02007A98` inside `0x020079B4`.
`UP (0x1B) → 0x02009890` and `POWER (0x24) → 0x02009F08` are at `0x020C2B68` / `0x020C2B98`.

**Live key state** (all in the `0x020C87xx` block): `0x020C87A8` last key id, `0x020C87A4` repeat
counter, `0x020C2598` debounce (last scanned key), `0x020C87D0` identical-sample counter,
`0x020C8780` fast-repeat flag, `0x020C87AC..0x020C87CC` per-key hold counters, `0x020C8784` raw ADC,
`0x020C8788` low-supply latch, `0x020C8794` "print blocked" latch.

**Correction to the earlier notes:** `0x020C8A34 / 0x020C8A38 / 0x020C8A3C` are **not** keypad state.
They belong to a stream/FIFO record module (record array at `0x020C8A3C`, stride `0x4040`, named
records with 8-byte FIFO entries). `0x020C8A34` is written once and never read (dead slot).
The `0x02004C00..0x02005958` block is that module's ops vtable (`0x020C26BC`), installed as the
framework's current device by `0x02000680` — not the keypad driver.

## Device framework

Device list head at CPU `0x020C2588` (`{u32 flag; u32 first_node}`); nodes are 0x18 bytes:
`{char name[12]; u32 fn0; u32 fn1; u32 status}`. `dev_ioctl` @ `0x02002A4C(idx, op, arg)` looks up
`node = first + idx*0x18` and calls `fn1(instance, op, arg)`.

| node | name | fn0 | fn1 |
|---|---|---|---|
| `0x020C259C` | `usb` | `0x02003250` | `0x02003208` |
| `0x020C25B4` | `battery` | `0x020034C4` | `0x02002C70` |
| `0x020C25CC` | `usensor` | `0x02002B90` | `0x02002BA8` |
| `0x020C25E4` | `sd-card` | `0x020031D8` | `0x0200317C` |
| `0x020C25FC` | `lcd` | `0x02002F20` | `0x020033F0` |
| `0x020C2614` | `led` | `0x02002EF8` | `0x02003398` |
| `0x020C262C` | `cmos-sensor` | 0 | `0x02002C34` |
| `0x020C2644` | `ad-key` | `0x02002E0C` (init) | `0x02003A60` (handler) |
| `0x020C265C` | `ir` | `0x02002EB4` | `0x020032D8` |

Device indexes resolved at boot: `battery`=+12, `usb`=+13, `sd-card`=+14, `ir`=+16, `led`=+17,
`lcd`=+19, `ad-key`=+20 (as offsets into the context at `0x020D416C`). There is **no printer device**:
printing is driven by app-level tasks, not the HAL device registry.

## Thermal printer

Module: CPU `0x02059000–0x0205AC00`. The head is **GPIO bit-banged** — no SPI, no DMA, no hardware
engine (all 66 port-SPR write sites in the image are in this module).

| Signal | Register | Bit | Notes |
|---|---|---|---|
| head DATA (serial) | PORTE `0x800C` | 15 (`0x8000`) | one bit per `lbz`-ed bitmap byte, thresholded |
| head CLK | PORTE `0x800C` | 13 (`0x2000`) | high/low per dot; 383 bitmap columns loaded per line, index 0 skipped (see the addendum) |
| head LATCH | PORTB `0x8004` | 5 (`0x20`) | low → high after the line is shifted |
| head STROBE (heat) | PORTA `0x8000` | 9 (`0x200`) | pulse width = adaptive, via `kc02_strobe_pulse` |
| head / print supply | PORTB `0x8004` | 6 (`0x40`) | on before the driver, off after |
| motor coil A / B | PORTE `0x800C` `0x1`/`0x2`, PORTF `0x8010` `0x400`/`0x800` | | 4-phase full-step |
| motor common | PORTA `0x8000` | 3 (`0x8`) | enable/common line |
| paper/cover sensor | PORTA `0x8000` | 7 (`0x80`) | input, read before and during printing |
| battery/DC sense | PORTA `0x8000` + ADC ch1 | 6 (`0x40`) | threshold ladder → print energy index |

Key functions (CPU): `kc02_delay` `0x020591CC` (r3×500 loops), `kc02_busy_delay` `0x02059208`
(r3×20), `kc02_strobe_pulse` `0x02059248`, `kc02_battery_check` `0x02059290`, `kc02_sensor_read`
`0x020593A4`, `kc02_load_print_image` `0x020593CC` (JPEG → YUV420 → head-sized canvas),
`kc02_sfr4800_save_restore` `0x0205980C` (13 bits of SFR `0x4800` bracketing each job),
`kc02_printer_helper_C80` `0x02059C80` (supply on/off), `kc02_motor_step` `0x02059D14`,
`kc02_printer_init` `0x02059F84`, `kc02_head_strobe_delay` `0x0205A124`,
`kc02_print_head_ramp` `0x0205A1FC` (main driver, 32 sub-strobes, ramp threshold),
`kc02_print_run_A` `0x0205A6C4` / `kc02_print_run_B` `0x0205A87C` (job wrappers; B = 1-bit 4×
over-print, selected by config byte 96 == 1), `kc02_print_layout` `0x0205AB70`,
`kc02_print_file` `0x0205ADF0` (12-byte "JRX" record header), `kc02_print_image` `0x0205B0FC`.

Print entry points and callers: `kc02_print_image` ← `0x0200A970` (UI task `0x0200A4BC`,
messages `0x1A`/`0x23` of the map at `0x020C2B50`); `kc02_print_file` ← `0x0200A810` /
`0x0200C360` (UI task `0x0200C24C`, message `0x01`). A job is
`printer_init → C80(1) → sensor_read/battery_check → [load+layout] → run_A|run_B → C80(0) →
state 0x020D2EC8 = 0 → 40 × {motor_step; delay(120)}`. Status codes: 0 ok, 1–6 supply/energy
levels, 8 no-paper/sensor, 9 low supply, 10 wrong bitmap height, 14 charging, 255 internal.

Bitmap model: **1 byte per dot** (never packed), `height` = 384 head dots, `width` = number of paper
lines, index `dot*stride + line`. Both drivers clock only rows 1..383 (`height-1`), so one dot row is
never latched — an open question worth scoping on hardware.

## Printer control details (resolved)

* **`SFR 0x4800` = `SPR_PICMR`** — the programmable interrupt-controller *mask* register (per `ax329x.h`).
  `kc02_sfr4800_save_restore` @ `0x0205980C` snapshots the individual mask bits used by the printer
  (bits `14,0,6,4,5,7,10,13,15,1,2,3,8`) into `0x020D2ECD`–`0x020D2ED8` — it reads `PICMR` and tests bit
  `0x4000` (bit 14) first — then re-applies changed bits through the generic helper at `0x0202ADD0`
  (`mtspr r0,r4,0x4800`). A print job therefore runs with a **modified interrupt mask** and restores it after.
* **`"JRX"` = the vendor's tag** (JinRiXin — the Shenzhen distributor/ODM behind this SDK, the same party the
  AppoTech material online points at). It appears as the build string `"JRX_20220309"` @ CPU `0x0207B9D8` and
  as the **magic of the 12-byte print-file record header**: written by the record/photo module at CPU
  `0x02056510`, validated in `kc02_print_file` at CPU `0x0205AF8C`–`0x0205AFA4` with byte compares against
  `74 ('J')`, `82 ('R')`, `88 ('X')`.
* **383 vs 384 dots (still open).** Both head drivers start the row loop at index 1 (`r4 = stride`) and
  `kc02_print_run_B` explicitly requires `height == 384` (`0x0205A9F8`) while clocking `height − 1` = **383**
  dots per line. Either the head has 383 usable dots, or row 0 stays in the shift register and is latched
  with the next line. Needs a scope check on a real unit (count CLK pulses inside one LATCH period).

## Image sensor (H63P)

The sensor is described by a **resource item**, not a code struct: header at CPU `0x020CB3B0`
(file `0xCD9B0`) `{items_addr=0x40, total=0x50, item_size=0x50}`, one 0x50-byte item at CPU
`0x020CB3F0` (file `0xCD9F0`):

| off | value | meaning |
|---|---|---|
| +0x00/+0x04 | `0x4980` / `0xC80` | ISP ops struct (flash offset/size) → RAM `0x020E32A0` (mclk 24 MHz, 1280×720) |
| +0x08/+0x0C | `0x5600` / `0xCA` | **init table** (flash offset/size) → RAM `0x020D1EA0` |
| +0x10/+0x14 | `0x3180` / `0x800` | RGB gamma table (`rgbgama[8][256] u8`) |
| +0x18/+0x1C | `0x3980` / `0x1000` | Y gamma table (`ygama[8][256] u16`) |
| +0x20/+0x24, +0x28/+0x2C | `0x2D08`/`0x478`, `0x2890`/`0x478` | LSC + AWB-class curve tables |
| +0x34 | `"H63P"` | sensor name, **inline `char[20]`** (hence no pointer references) |
| +0x48 | `0x48` | expected ID value |
| +0x49 / +0x4A | `0x80` / `0x81` | I²C write / read command byte (7-bit slave `0x40`) |
| +0x4B / +0x4C | `0x01` / `0x01` | register-address width / data width (bytes) |
| +0x4E | `0x000B` | ID register (`0x0B`) |

Bring-up: `sensor_init` `0x020583F0` (LDO, CSI MCLK 16 MHz for the probe, IIC1 @200 kHz, autocheck,
then MCLK 24 MHz and the init-table loop) → `sensor_autocheck` `0x02058170` (reads the resource,
probes reg `0x0B` for `0x48`, loads blobs, **falls back to an internal test-pattern descriptor
`0x02083CB0`/`0x02083D00`** on failure) → `sensor_read_reg`/`sensor_write_reg` `0x02058088`/`0x02057FB4`
→ CSI hand-off `csi_register_sensor` `0x020381C8` (stores `{item, ops}` at `0x020D7F38`, consumed by
`FUN_02032844` from the CSI task).

**H63P init table**: file `0xD2FB0` (CPU `0x020D09B0`), 100 `{reg, val}` pairs + `FF FF` terminator,
starting `0x12,0x40` … `0x12,0x00` (reset assert/release). Full annotated table in
`board-sensor.md` §3.

Family: register map and PLL/window/analog blocks match the SDK's **H62 / H65** drivers (and H7640);
values differ (PLL `0x0F/0x10`, window `0x20..0x2B`, extra early/late analog blocks). The SDK has no
H63P driver — it is a board-specific member of that vendor family.

## Modes, UI and update plumbing

- `ui_mode_request` @ `0x02008108` (28 call sites). Boot (`file 0x2AAC`) requests mode 6 (main menu).
- POWER handler `0x02009F08` (key `0x24`): `r4 == 1 && payload == 0` → `ui_mode_request(6, 0)`, then
  state bytes `0x020D41D4 = 2`, `0x020D41D5 = 0` — as documented in `CHILD_MODE.md`.
- SD-update verify `0x020026C8` (file `0x4CC8`): compares `0x800`-byte chunks, prints progress %,
  failure string `"upgrade failed"` @ `0x0207B893`.
- Photo pipeline strings: `0x020C1D28` "write yuv file start", `0x020C1DD1` "image ok",
  `0x020C1F40` "UPRIGHT_4", `0x020C1F64` "thum error", `0x020C1FA8` "jpg decode fail".

## Analysis tooling

- **Disassembly** is authoritative: `or1k-elf-objdump -D -b binary -m or1k --adjust-vma=0x02000000 app.bin`
  (add `--adjust-vma=0x01FFDA00` for a whole-image listing). Binary at
  `~/.src/ax3292-toolchain/out/prebuilt-or1k-elf/bin/`.
- **Ghidra**: language `KC02:LE:32:nodelay` (derived from `smaeul/ghidra-or1k`, which is big-endian with
  delay slots and cannot decode this firmware). Import raw at base `0x01FFDA00`.
- **`spr` space**: `l.mfspr`/`l.mtspr` are modelled as memory operations in a dedicated `spr` space with
  a 64 KB `SPRREG` block and all 1089 SFR names from `ax329x.h` as symbols.
- **Volatile fix (required for readable driver code)**: the decompiler treated `spr` varnodes as
  unmapped/non-volatile, so peripheral reads became phantom `in_spr_*` inputs and stores were dropped as
  dead. Memory-block settings (initialized, r/w, volatile, size) make **no** difference; the fix is an
  additive `<volatile>` range in `OpenRISC.pspec`:

  ```xml
  <volatile outputop="write_volatile" inputop="read_volatile">
    <range space="spr" first="0x0" last="0xffff"/>
  </volatile>
  ```

  With it, `kc02_motor_step` decompiles to real PORTE/PORTF/PORTA read-modify-writes.
- **Function entry heuristic** (validated on 2609 functions): a real entry is the address immediately
  after an `l.jr r9`, cross-checked against `l.jal` targets. Creating functions at mid-body addresses
  yields garbage decompilation.

## Open items

1. ~~Printer: which dot row is skipped (383 vs 384)~~ — **resolved on hardware (2026-10), see the addendum below**; the "JRX" record header
   format; why SFR `0x4800` is bracketed around jobs; `0x020D2EC8` has no reader in the image.
2. Sensor: which of the two `0x478` tables is LSC vs AWB; the full SFR list programmed by `FUN_02032844`
   (CSI/ISP capture config).
3. Keypad: the message strings behind print status ids `0x57`/`0x58`/`0xBB` live in the compressed UI
   resource pack (no printable ASCII for "paper"/"print" exists).


---

## Device-confirmed addendum (2026-10)

The image-sensor identification in this document is no longer inference from resource bytes: it was
**verified against the physical sensor** by running the firmware's own bring-up path on the camera (payload
`docs/hw/payloads/sensorprobe.c`, read-only with respect to firmware state, LDO powered only for the probe):

* `sensorAutoCheck` (`0x02058170`) returned the **real** sensor ops `0x020E32A0` - it only does so when the
  probe register read equals the resource's expected id, i.e. the part answered **`0x48`**.
* Probed register **`0x0B`**, 7-bit I2C slave address **`0x40`**, inline name **`H63P`** (item +0x34),
  expected id **`0x48`** (item +0x48), init table **202 B / 100 write pairs** (item +0x0C), first pairs
  `12/40 48/85 48/05 0E/11 0F/84 10/20`.
* The driver's RAM copies (`0x020E4BA0`, `0x020D1EA0`) are **byte-identical** to the flash resources
  (`0x020CB3F0`, file `0xD2FB0`), confirming both the resource layout and the copy path.


---

## Printer dot-column question — resolved on hardware (2026-10)

A test bitmap was printed **directly from RAM** (payload `docs/hw/payloads/ramprint.c`: `hal_sysMemMalloc`
canvas, luma row-major `canvas[head_pos*w + line]` with `0x00` = ink, then `kc02_print_run_A(canvas, w, 384,
energy, threshold_mode, param6)`, which performs its own pin setup, head power, strobe ramp, paper check and
40-step feed). Pattern used: five 1-dot-wide lengthwise lines at head dots **0, 4, 8, 12 and 383**.

Observed on paper: **four lines on the left, one on the right — all five printed.** Therefore:

* **The print drivers load 383 bitmap columns per line, starting at bitmap index 1.** Both
  `kc02_print_head_ramp` (`0x0205A1FC`: `uVar6 = stride; while (uVar6 < stride * rows)`) and
  `kc02_print_run_B` (`0x0205A87C`: `uVar1 = stride; while (uVar1 < stride * 0x180)`) begin at
  `stride x 1`, so **bitmap index 0 is never loaded**. This supersedes the earlier paper-based reading
  that no column is dropped: one 0.125 mm column cannot be told apart by eye, so the paper test could
  not settle it. Confirming what the unused shift-register position carries needs an electrical check
  of CLK pulses per LATCH on `PORTE.13`. For a custom print path the practical consequence is that a
  384-column canvas can only ever place 383 columns.
* **The dot order is mirrored.** Drawing with increasing bitmap index prints the image reversed
  left-to-right, so bitmap index 0 lands on the paper's **right** edge. The RAM print payloads
  therefore index the canvas as `384 - 1 - x` (`phys_x` in `docs/hw/payloads/artprint.c` and
  `probe.c`). This supersedes the earlier "no mirroring" note here; the stock `kc02_print_file` path
  compensates internally, which is why printed camera photos are not mirrored.

Two hardware limits were found while doing this, both of them supply-related and both **resolved by the
battery-connector repair**: **solid fills** (a full-width bar is a whole line of head current) did not
transfer on USB-only power, while a ~50-dot-per-line pattern did; and camera mode was not sustainable without
the battery. After the repair both work, and the limits are now understood as power-source limits rather than
head limits - see [hw/post_repair/README.md](hw/post_repair/README.md) and
[HARDWARE_OBSERVATION.md](HARDWARE_OBSERVATION.md).

Also validated: the print job can be driven entirely from a payload with **no SD card, no gallery and no mode
change** (status 0 = ok, 8 = no paper / paper-cover sensor, 9 = low supply, 10 = wrong bitmap height,
14 = charging, 255 = internal), which makes the printer testable in ~10 s per iteration.
