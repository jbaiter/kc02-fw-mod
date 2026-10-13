# Hardware observation — method and first results (2026-10)

The camera (USB `0219:3280`, single Mass-Storage/SCSI interface) was attached and read **read-only**: the
stock vendor command `0xCD` with `CALLBACK_SKIP`, direction device→host, firmware routine signatures
verified before the first transfer, no RAM writes, no RAM code execution. Observer script:
`/tmp/kc02-board-re/hw/observe_state.py` (subclasses the repo's `SurveyTransport`; every read must fall inside
a declared region). Raw snapshot + hashes: `/tmp/kc02-board-re/hw/snapshot01/`.

## Region lengths must be evidence-based (corrected 2026-10)

Reads are only as trustworthy as the region table. One entry was wrong and had been quietly
reporting a neighbour's values as fields:

* `printer_state` was declared as `0x020D2EC8 + 0x20`. A whole-image scan of address
  materialisation finds **exactly one access to `0x020D2EC8`**: a single 4-byte store in
  `print_run_A`, with **no reader anywhere in the image**. The 28 bytes after it are separate
  globals, including a 1-byte counter at `0x020D2ECC` and unrelated words at `0x020D2EDC/E0/E4`.
  Reading `0x20` bytes therefore produced plausible-looking numbers that were not printer state
  at all. It is now `printer_counter` (4 bytes).
* `app_context` was declared as `0x020D416C + 0x78`, which overlaps `ui_queue` at
  `0x020D41E0`. It is now `0x74`, so the two regions no longer overlap and nothing is
  double-counted.

`docs/hw/scan_ram_access.py` derives the table from the listing instead of guessing:

```sh
python3 docs/hw/scan_ram_access.py /tmp/kc02-board-re/full.dis 0x020D2E9C 0x020D2EF0
```

It finds the `l.movhi rN,0x20d` .. `l.ori rN,rN,<offset>` constant-materialisation pairs and then
attributes the memory operands that use that register, reporting the address, the access width
from the opcode (`lbz`/`sb` = 1, `lhz`/`sh` = 2, `lwz`/`sw` = 4), the site count and how many are
reads versus writes. Two limits to keep in mind: it only sees **absolute** addresses, so struct
fields reached as base+offset from another base are not attributed, and a large attribution
window eventually produces false positives when the register is reused, so keep the window small
(50 instructions is enough for the printer code and 600 is not).

Rules for the region table, now written next to it in `observe_state.py`: derive the length from
evidence, never pad a region to cover a neighbour, and add a separate entry instead. Every region
is also kept 4-byte aligned and a multiple of 4 bytes, because the transfer path uses fixed-size
aligned moves.

## Confirmed against the device

* **Keypad ADC windows.** `adc_key_table` @ `0x020C268C` read as
  `key_id=0x1A centre=386 lo=351 hi=421`, `key_id=0x1E centre=776 lo=741 hi=811` … i.e. **exactly ±0x23**
  around the centre, which is what `kc02_adc_init` computes statically. The `kc02_adc_key_entry` struct and
  the six windows are now device-verified, not inferred.
* **The 10 modes and their labels.** The live mode table at `0x020CCD5C` (a runtime-initialised pointer table)
  points at ten mode descriptors whose first word is a string pointer. The labels read out of the image are
  literally: `Power Off`, `Video Recorde`, `Photo Encode`, `Play Back`, `Audio Player`, `Main menu`,
  `Game menu`, `Game`, `Setting menu`, `usb device` — the same ten tasks the static map predicted.
* **Mode descriptor layout.** Each descriptor is `{char *label; u32; fn op1; fn op2; fn op3; u32 *keymap;
  u32 *after_map; u32; u32}` — `op1/op2/op3` are the enter/exit/tick trio (already named in the project as
  `mode_<mode>_enter|exit|tick`), and the `{msg,handler}` keymap follows the descriptor inline (+0x20 … +0x44
  depending on the descriptor's own trailing tables). Live agreement: for the 10 descriptors, the map found
  behind each one is byte-identical to the map we had typed statically at `0x020C2B50`, `0x020C2FB4`,
  `0x020C4C3C`, `0x020C4E40`, `0x020C4F14`, `0x020C542C`, `0x020C6520`, `0x020C3354` …
* **Message-id semantics** (from the union of the ten maps): `0x0E` = mode enter/init, `0x0F` = mode
  exit/free, `0x1A`/`0x23` = OK, `0x1B`/`0x1C` = up/down, `0x1D`/`0x1E` = left/right, `0x24` = back
  (mode-6 request), `0x01`–`0x06`, `0x09`, `0x0A` = system messages (USB insert/eject, SD, time update).
* **The dispatch table is a runtime overlay.** `0x020CCC8C` holds the 46-word (`0xB8`) handler table filled by
  `msgFuncRegister`. In the **image** those bytes are not a table at all — they are a 432-byte curve
  (`0x94…0xFF` ramp, then a non-linear `0x00…0xFF` ramp, `0xFFFF…` tail). So the firmware deliberately
  reuses that `.data` curve as the message-dispatch area at runtime: any static typing there must be marked
  "runtime overlay, image content is a curve". The live table then contains **only the active mode's** entries
  (the camera was in USB mode: 9 non-null entries, all `usbDevice*` handlers + `sysMsgMenu`).
* **Runtime state read**: `app_context` @ `0x020D416C` = `05 01 02 00 04 00 01 00 …` (byte 0 = `5` = the Audio
  Player mode at that moment), `sensor_item` @ `0x020CB3F0` all-`0xFF` (sensor driver not initialised while the
  imaging pipeline is off), `printer_state` zeroed, first device node name = `"usb"`.

## New function entries discovered (census gap)

The mode maps reference handler entries that our function census never delineated — the bytes are unambiguous
(`l.jr r9` immediately followed by a prologue):

```
0x02016970: 44004800        l.jr r9      (end of the previous function)
0x02016974: d7e177e8 ...    prologue     <- referenced by the settings keymap as msg 0x0E
```

Instead of separate functions the project currently has a few **merged blobs**: `mode_settings_enter`
@ `0x02015DB4` swallows `0x0201686C`/`0x02016890`/`0x02016974`; `ui_request_mode6_arm_b68_c` @ `0x02015074`
swallows the audio handlers (`0x02015878`, `0x02015B64`, …); `usbDeviceKeyMsgMode` @ `0x0200D408` is 24 bytes
long and ends exactly where `0x0200D420` begins. Splitting those regions at the map-verified entries both
fixes the census and gives each handler a name by construction (`<mode>_msg<hh>_<role>`).

Nine handlers that *were* defined functions were named this way already in this session
(`playback_msg01/03/06`, `main_menu_msg0a`, `game_msg0e/0f/1a/1b/1c`; 80 → 71 unnamed).

## Next hardware steps (ranked)

1. **Split the merged handler regions** at the map-verified entries, then name them by (mode, message).
   Needs care: creating a function inside an existing body must not lose the original's other blocks.
2. **Differential RAM sampling**: re-read the state regions around user actions (enter each of the ten modes,
   press each key) and diff — verifies `kc02_app_context` field semantics and the per-mode state globals, and
   names the 71 remaining functions by correlating the global they write with the mode that was active.
3. **Sensor identity over IIC** (`H63P` reg `0x0B == 0x48`) — needs code execution (a harness case), not just
   a read; the sensor item is `0xFF` until the imaging pipeline runs.
4. **Printer 383-vs-384 dot row** — print a test pattern on stock firmware and measure the paper.
5. **Byte-identical rebuild flash** via the SD updater (`DestBin.bin`) — proves build→package→flash→boot with
   zero behaviour change; the gate to a from-scratch image.


## Update: on-device code execution and remote UI control (2026-10)

The read-only observer was extended to **execute our own compiled code** on the camera, reusing the
proven layout and safety envelope of `tools/toolchain_tests` (`SHIM 0x020D8000` -> `l.j` -> code at
`0x020D8100`, 5632 B verified-free; 2 KiB execution packet at `0x020D9700`; dispatcher/entry conventions
from `payloads/common.h`). Executor: `/tmp/kc02-board-re/hw/execute_payload.py` (mirrored in
`knowledge/hw/`). Guard sequence, identical in spirit to the repo harness: pinned firmware signatures
(incl. the full I-cache helper body) -> return-only canary executed and its buffer verified -> dispatcher,
code and packet each uploaded **with RAM read-back** -> stock I-cache reset (`0x0202B014`) -> execute.
Writes are byte-whitelisted, reads are limited to pinned sites or declared blobs, no other callback is
accepted, and every step fails closed.

Payloads built and run (`payloads/*.c`, all freestanding, linked at `0x020D8100`):

* `echo` (160 B) - computes from the packet and reads one firmware word: proved **our compiled code runs on
  the silicon**, and its read of `0x020D416C` (`0x00020105`) matched the independent read-only snapshot
  exactly.
* `mode` (212 B) - materialises `0x02008108` and calls it, i.e. the firmware's own
  `ui_mode_request(mode, param)`.
* `camprobe` / `camprobe2` (388 / 560 B) - request a mode, wait with the firmware's own `XOSTimeDly`
  (unit confirmed = ms via `XOSTimeGet`), then sample state into the packet and/or a **RAM mailbox** at
  `0x020DB300` (inside the measured all-zero `0x020DB200`-`0x020E30FF` span, no referenced globals) with
  `CACHE_clean` for DMA visibility.

### Results

* **Remote UI control works.** `ui_mode_request(6,0)` moved the camera from the USB/SD screen to the main
  menu (visually confirmed on the device), and `ui_mode_request(3,0)` then moved it into **camera mode**.
  This is a new capability: the UI can be driven from the host while connected.
* **The request is serviced only after our command returns.** The payload executes inside the app's own
  USB-command task, so the app cannot process the mode request until the payload returns; the switch
  therefore always lands *outside* the observation window. The mailbox pattern exists for exactly that:
  write state to RAM, let the USB drop, re-enumerate, read it later.
* **Entering camera mode tears down the USB function.** The mass-storage interface disappears (and on
  re-enumeration the kernel binds `usb-storage` to interface 4, which then makes the vendor command
  `EBUSY` until the driver is detached).
* **With the battery connector broken (USB-only power) camera mode is not sustainable** - the unit drops
  off the bus entirely, consistent with a power/brown-out under LCD + imaging load with no battery to
  buffer. Camera-mode observation should wait for the battery fix.

### Correction to the static model

`0x020CCC8C` is **not** the per-mode key dispatch. Live evidence: the table still contained the USB-mode
handlers (`msg 0x02 -> usbDeviceSysMsgUSB`, `0x1A/0x23 -> usbDeviceSysMsgSD`, ...) while the device was
visibly showing the main menu. It is the **system-message** dispatch (USB/SD/time/builtin, filled by
`msgFuncRegister` from `task_map_builtin` + a `msgDealInfor` array); per-mode keys are dispatched through
the active mode descriptor's `keymap` pointer, which is why the mode descriptor table (labels, enter/exit/
tick ops, inline `{msg,handler}` maps) is the authoritative UI map - as the earlier snapshot established.

## Read-only audits completed (2026-10)

**Image integrity.** 855 040 bytes read from SDRAM (`0x02000000`-`0x020D0C00`) and compared with the flash
dump: **23 of 417 chunks differ**, and only in the two regions that are legitimately built at runtime -
`0x020C2000`-`0x020C3800` (ADC key table + device list) and `0x020C6800`-`0x020D1000` (mode table, dispatch
overlay, `app_context`). Everywhere else the running image is byte-identical to `firmware/original.bin`,
which also confirms the load mapping `CPU = file + 0x01FFDA00` on real silicon.

**Structural validation of the typed model**, from live bytes (all pass):

| element | result |
|---|---|
| `kc02_adc_key_entry` x6 | `lo = centre - 0x23`, `hi = centre + 0x23` exactly; centres OK 386, RIGHT 776, LEFT 252, UP 657, DOWN 510, POWER 12 |
| dispatch table `0x020CCC8C` | 9 non-null entries, every one a valid code pointer |
| mode table `0x020CCD5C` | 10 descriptors, labels decode as `Power Off`, `Video Recorde`, `Photo Encode`, `Play Back`, `Audio Player`, `Main menu`, `Game menu`, `Game`, `Setting menu`, `usb device`; each has three code ops |
| mode lifecycle ops | now known by address, e.g. `Photo Encode`: `mode_photo_enter 0x020082F4`, `exit 0x020083C8`, `tick 0x020084A0` |

**Correction: the sensor block is flash-resident, not RAM.** `sensor_item 0x020CB3F0` has real content in
flash (`80 49 00 00 80 0c ...`) but is all-`0xFF` in RAM; the ops blob `0x020CFD30` is data in flash and
zeros in RAM. They are **flash resources** read on demand by `sensorAutoCheck` (`0x02058170`) via
`flashRead(0xCD9B0, ...)`, copied to `0x020E4BA0` / `0x020E32A0` / `0x020D1EA0` only during sensor bring-up.
Reading `0xFF` there was therefore correct behaviour, not a driver failure.

## DONE: the image sensor is confirmed on hardware (H63P, reg 0x0B == 0x48)

`docs/hw/payloads/sensorprobe.c` (488 B) runs the firmware's *own* bring-up pieces in the right order -
LDO power `0x0202B284(6,1,0)` -> CSI MCLK 16 MHz `0x020381A8` -> IIC enable `0x02038730` ->
`sensorAutoCheck 0x02058170` (which reads the sensor ID register and compares it with the flash resource's
`0x48`) -> an independent IIC read of reg `0x0B` on slave `0x40` - deliberately **skipping** the CSI-task
registration so no capture pipeline starts, and switching the sensor LDO off again before returning. Its
result distinguishes "real sensor answered" (`ops == 0x020E32A0`) from "test-pattern fallback"
(`ops == 0x02083CB0`), and dumps the RAM item copy so the resource layout can be verified too.

Run it with a stable device:

```bash
cd ~/.src/kc02-fw-mod
./.env/bin/python docs/hw/execute_payload.py --payload sensorprobe --input-words 1
# or wait for enumeration and fire immediately:
docs/hw/wait_and_probe.sh
```

### Result (probe ran, device enumerated as `Bus 008 Device 004`)

```
sensorAutoCheck returned ops = 0x020E32A0     (real sensor; the 0x02083CB0 test-pattern fallback would
                                               mean the ID probe failed -> so the sensor answered)
RAM item  0x020E4BA0 == flash item 0x020CB3F0 : True
RAM table 0x020D1EA0 == flash table 0xD2FB0   : True   (202 B = 100 write pairs + terminator)
item +0x34 = 'H63P\0'   item +0x48 (expected id) = 0x48   item +0x4E (probed register) = 0x0B
```

So the **H63P** identification is confirmed against the physical part, not just the resource bytes: 7-bit
I2C address `0x40`, identification register `0x0B` expected value `0x48`, 100-pair init table starting
`12/40 48/85 48/05 0E/11 0F/84 10/20 …`. The driver's flash->RAM copies are byte-identical to the flash
resource, which independently validates our model of that resource block.

One caveat for the record: my own direct IIC read of reg `0x0B` (`hal_iic08bitAddrReadData(0x40,0x0B,…)`)
returned `0xFF` - inconclusive, most likely a wrong address-mode/command for this part. The authoritative
signal is `sensorAutoCheck`'s own probe, which uses the resource's `r_cmd`/`addr_num` fields and matched.

### Remaining hardware limitation

With the battery connector broken the camera enumerates and then drops again within seconds to a couple of
minutes (observed device numbers 099, 105, 106, 112, 003, 004) - consistent with a dead-battery
auto-power-off or a brown-out under load. Camera mode, camera-mode observation and all flashing wait on the
connector repair; the tooling and payloads are already in place for when it is fixed.


## Additional read-only sweep: `.bss` and the RAM-globals inventory (2026-10)

`.bss` `0x020D0C00`-`0x02103980` (208 256 B) read: only **4.5 % non-zero** - consistent with an idle
device in USB mode. Classifying all 1100 entries of our RAM-globals inventory that fall in that window:
440 zero, 43 pointers into the image region, 49 pointers into RAM, 74 small integers, 77 other. Live
cross-checks that fall out of it:

* `0x020D7F48 = 0x020E32A0` - the **sensor ops pointer the probe's `sensorAutoCheck` selected**, i.e. the
  driver really did identify the H63P and published the ops; `0x020D73E0/0x020D73F0/0x020D73FC` likewise
  point into the driver's `0x020E3xxx` RAM copies.
* The **HAL pool start `0x02103980` is no longer all-zero**, so the allocator is live there - independent
  confirmation of the measured map's conclusion that this range is not scratch space.


## RAM-print pattern suite (2026-10)

> **The USB-power numbers in this section predate the battery-connector repair.** On USB-only
> power the head could mark only ~8 dots per line; after the repair the same staircase prints all
> 16 bands through 64 dots per line and full-width bars cover the whole head. Treat the limits below
> as power-source limits, now resolved. Current results:
> [hw/post_repair/README.md](hw/post_repair/README.md).

All printing is done **from RAM** - `docs/hw/payloads/ramprint.c`, pattern selected by `INPUT[5]`, printed
via `hal_sysMemMalloc` + `kc02_print_run_A(canvas, w, 384, energy, threshold_mode, param6)`. No SD card, no
gallery, no mode change; each print is ~10 s and reports the firmware's status (0 = ok, 8 = no paper/cover
sensor, 9 = low supply, 10 = wrong bitmap height, 14 = charging, 255 = internal) plus the canvas address and
the ink-pixel count, which is how each print is validated.

| id | pattern | purpose | observed |
|---|---|---|---|
| 0 | 1-dot edge line + control at dot 16 + three full-width bars | head-width / dropped-column test | on USB-only power the bars did **not** print (a full-width bar is a whole line of head current); the 1-dot lines did. After the repair the bars print across all 384 dots |
| 1 | counting comb (49 dashes at dots 0,8,…,376,383), four 1-dot lines at dots 0/4/8/12, right-edge line | count-based edge test | all five lines appeared (4 left + 1 right). **Superseded as a conclusion:** the print drivers load 383 bitmap columns starting at index 1, so bitmap index 0 is never loaded, and a single 0.125 mm column is not distinguishable by eye. See the BOARD_RE addendum |
| 2 | 7-segment "3292" + 8-step hatched density ladder + corner ticks (`w=128`) | text legibility, printable grey range | printed, `ink_px=3104` (matches the drawing) |
| 3 | 1 mm feed ticks at both edges (5 mm marks) + 48-mark head-axis comb (`w=128`) | dot-pitch / feed-pitch measurement | printed, `ink_px=670` |

Notes for a custom print path: canvas is luma, row-major, `canvas[head_pos*w + line]`, `0x00` = ink and
`0xFF` = white; the head inks when `luma <= threshold`, with a 32-step threshold ramp per line and an
adaptive strobe whose length depends on the number of set dots and the energy byte. Keep the dots-per-line
low (a few dozen) when the device is on USB power without a battery; sparse patterns print cleanly at
`energy=3`, and dense patterns need the battery (resolved after the repair - see the note above).


### Print-density limit on USB power (2026-10) - established by axis separation

First hypothesis was wrong twice over (it claimed the strobe shortens as the dot count rises - the code does
the opposite, `tbl[i] = i/divisor` indexed by `2*count`, i.e. it lengthens the pulse to compensate sag - and
the patterns confounded *position* with *count*, because every dense feature had also been in the middle of
the head). Two purpose-built patterns separated the axes:

**Position axis** (pattern 6a + 7a): eight 4-dot marks at head dots 4/64/128/192/256/320/352/376, and four
4-dot marks at uniform 48-dot spacing. Result on paper: **all marks present, evenly spaced, spanning the full
width** -> the head drives its whole length and the dot pitch is uniform (1:1 with the canvas).

**Count axis, position held constant** (pattern 6b: left-aligned bars of 2,4,8,...,256 dots; pattern 7b/c:
identical 24- and 48-dot bars placed left-aligned *and* centred):
* 4 and 8 dots per line print; 12, 24, 48, 72 and 383 dots per line do not.
* The 24- and 48-dot bars failed **both** left-aligned and centred, i.e. at the very edge where 4-dot marks
  print -> the effect is the **dot count**, not the position (the rail-sag-along-the-head theory is dead).
* The firmware is not throttling: the print status code reads **14 = charging** and tracing
  `kc02_print_head_ramp` shows that code yields the **maximum** strobe bias term, so the drive is already at
  its strongest setting.

**Conclusion: on USB-only power the head can mark at most ~8-11 dots per line (~2 % of 384)**; fine detail is
unaffected (1-dot lines and 0.125 mm features print crisply, pitch uniform), but text, fills and any real
image need the battery's current. Print-quality work (density ramp, dithering, a photo) is therefore queued
behind the battery-connector repair, together with camera mode and the rebuild flash. The whole suite is
reproducible from `docs/hw/payloads/ramprint.c` (patterns 0-7) in ~10 s per print.


### Dot-count staircase: the measured USB-power budget

Printing works at any density the head can drive, but on **USB-only power the transferable ink is limited**:
the strobe length is inversely related to the number of set dots per line (constant-power model, `tbl[i] =
i/divisor` feeding the strobe width), and the rail sags further under load. Observed correlation on the
`ramprint` suite: 2-8 dots per line (corner/edge ticks) printed; ~48-72 dots per line (7-segment digits, the
hatched density ladder) and 383 dots per line (full-width bars) did not. So on USB power only *sparse*
patterns transfer - text and fills need the battery.

To quantify the budget, pattern id 5 is a **dot-count staircase**: 16 bands of 1 mm along the feed, band k
carrying `4 + 4k` dots per line (4 … 64), so the highest band that still shows ink gives the maximum
transferable dots per line. (`ink_px` for that pattern is 4352, which is exactly the computed sum, so the
staircase can be validated from the report alone.)

**Measured result: only the first two bands printed (4 and 8 dots per line); the 12-dot band and everything
above it did not, at the longest strobe setting (`energy=3`).** So on USB-only power the transferable ink is
about **8 dots per line - roughly 2 % of the 384-dot head**. That is a hard limit of the power source (strobe
shortens as the dot count rises and the rail sags under load), not of the head: 1-dot lines and 0.125 mm
features print crisply. Practical consequence: **text, fills and any real image require the battery**; only
sparse marks (a few dots per line) can be printed while USB-powered.

**Resolved after the battery repair:** all 16 bands print, so this was a supply limit and not a
head limit. The staircase is still the right functional check for whatever power source is in
use, but 8 dots per line is no longer the expectation on a repaired unit.


## Battery connector / power note (pre-repair, 2026-10)

The battery connector is **2-pin** (B+ / B-): there is no third sense pin, so the battery **level** is sensed
on-board - `PORTA.15` is the DC/charge detect (USB side) and **ADC channel 1** is fed by a divider tapping the
battery rail (BOARD_RE: "battery/DC sense | PORTA 0x8000 + ADC ch1 | threshold ladder -> print energy
index"). Consequence for the repair: restoring B+/B- also restores the level reading, and since that level
selects the print energy index, a damaged sense trace would show up as faint printing even with a good
battery.

Pre-repair, and still after the repair, USB power makes `kc02_battery_check()` report status
**14 = charging**, including with the battery connected. That code is therefore **not** a
battery-absent indicator and must not be read as one; the repaired unit printed ~15 jobs at
status 14, including full-width and dense patterns. Past the 2-pin connector there is still no
third sense pin, and the level comes from an on-board divider into ADC channel 1.

`docs/hw/payloads/battcheck.c` reports the status code with several raw ADC samples and
`battery_read()`. The functional check is pattern 5, the dot-count staircase: after the repair all
16 bands print, which is the evidence that the supply is no longer the limit.
