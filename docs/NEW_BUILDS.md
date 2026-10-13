# Newer KC02-class builds: MYSTILUCK X8D and HMOLLIMT Z-12

First-pass map of the two newer units, both of which have a second (selfie) sensor and a row-based
settings menu. Dumps and their hashes are in [../firmware/README.md](../firmware/README.md); the dump
method is in [BLACKBOX_DUMP.md](BLACKBOX_DUMP.md).

Status: **imported and analysed in Ghidra.** The platform, mapping, address errors, asset tables and
the three programs below are established. The two features that motivated this work - the second
sensor and the row-based menu - are now the next step, and the programs are ready for it.

## Ghidra state

Both images are imported into the shared project as additional programs, so the original is
untouched. Same language (`KC02:LE:32:nodelay`) and same base (`0x01ffda00`), which makes raw file
offsets land on their runtime CPU addresses.

| Program | Functions | Named | How |
|---|---|---|---|
| `218c6cdfe59932d1.bin` (original) | 2314 | 2243 | earlier naming work |
| `mystiluck-x8d-8e0636e1.bin` | 2915 | 0 | entry candidates + `Kc02CreateStarts` |
| `hmollimt-z12-10da5dbf.bin` | 3447 | 0 | entry candidates + `Kc02CreateStarts` |

**Auto-analysis alone is not enough, and this is worth knowing for any new build.** A raw-binary
import gives Ghidra no entry points, and its analyzers only reach code through constant and data
references: that found **426** functions in the X8D against roughly 2300 real ones, and all 112
analysis options are identical to the original's, so the original's function set did not come from
different analyzer settings either. The Aggressive Instruction Finder, driven directly via
`Analyzer.added`, returned `ok=true` in 95 ms and created nothing. The memory block *is* marked
executable, so that was not the cause.

What works is decoding the entry points from the instruction encoding with
`tools/sfat/or1k_function_starts.py` and creating functions from that list with
`tools/sfat/Kc02CreateStarts.java`:

* `l.jal` (opcode `0x01`) targets are called functions;
* the instruction after `l.jr r9` resembles the next prologue. Note `l.jr rB` keeps `rB` in bits
  15..11, **not** 25..21 - reading it from the wrong field reports zero returns and looks like the
  image has no function epilogues at all.

Counting `l.jr r9` returns gives 2328 (X8D) and 2429 (Z-12), close to the numbers in
[../firmware/README.md](../firmware/README.md). The candidate lists are supersets on purpose: data
that decodes as a jump adds noise, and Ghidra's own disassembly prunes most of it, but the totals
above include some non-functions.

## Name propagation from the original

The two new programs are no longer unnamed: **883 names** (X8D) and **910** (Z-12) were propagated
from the original by masked-opcode matching.

| Program | Functions | Named | Share of functions |
|---|---|---|---|
| `mystiluck-x8d-8e0636e1.bin` | 2915 | 883 | 30 % |
| `hmollimt-z12-10da5dbf.bin` | 3447 | 910 | 26 % |

Method (`tools/sfat/propagate_names.py`): the builds are relocated, so raw bytes rarely match.
Each function is fingerprinted by its first 32 instruction words after masking the immediate and
address fields:

```
word & 0xFFFF0000
```

That zeroes 16-bit immediates, 26-bit branch targets and 16-bit `movhi`/`ori` halves, leaving
opcode and register fields. A name is propagated only when the fingerprint is **unique in the
original and unique in the target**, so an ambiguous match is dropped rather than guessed.
Precision here rests on uniqueness rather than a similarity threshold, because the same masked-word
idea scores ~0.81 for *unrelated* functions in the SDK alignment work.

Two exceptions per target were reported and left alone: 19 (X8D) and 16 (Z-12) fingerprints with
more than one candidate in the target, plus 682 and 658 original functions with no unique
counterpart in that build. The latter means either the function is absent from that product or it
changed enough to break a 32-word prefix; it is not evidence of either on its own.

Windows of 32 instructions measured best. Shorter prefixes match *fewer* functions, not more,
because a short prefix collides across unrelated functions and then fails the uniqueness test -
4 words gave 217 names against 883 at 32.

### Verification

Two independent checks, both passed:

* **The SPI reader.** Each build's `hal_spiFlashRead` was located earlier by byte signature, with no
  reference to the name table: `0x0203D38C` (original), `0x02037004` (X8D), `0x02035A10` (Z-12).
  Propagation independently produced exactly those three addresses under exactly that name.
* **Semantics.** The function is 200 bytes in all three builds and decompiles to the same sequence -
  the same `ax32xx_spi0SendByte` calls with the same command byte `3` and the same address byte
  order. The only difference is that the chip-select setup is a named call in the original and an
  unnamed `FUN_` in the new builds, because that particular function is not one of the matches.

Applied in place with `tools/sfat/Kc02ApplyPropagated.java`; every propagated name landed on a real
function (`applied=883/910, missing=0, skipped=0`), and each carries a provenance comment.

**The remaining ~70 % of functions in each build are still unnamed.** The propagated set is the part
that is unchanged between products, so it is also the part that tells you least about what is new.
The sensor and menu work will be in the remainder.

## What is settled

### Same platform, same mapping

All three images are little-endian **OpenRISC** of the Buildwin AX329x family, and the mapping is
**identical**:

```
CPU = file_offset + 0x01FFDA00        (application)
```

Confirmed for the new builds, not assumed:

* The Z-12's located SPI reader lands at `0x02035A10` from file `0x038010`, giving the constant
  directly.
* The X8D's `cmd_ctx` at `0x02085E14` is materialised by **15 real code sites**
  (`l.movhi`/`l.ori` pairs), so the same constant holds there.

So addresses in the two new dumps can be read with the tooling already in the repo. Byte-identical
code is present in all three builds (cross-correlation of 32-byte chunks matches hundreds of
positions), and the SPI reader's body signature is identical, which is why one dump recipe covers
all of them.

### The X8D self-identifies as Ax3295B, not AX3292

`Ax3295B-V3.0` appears in the X8D; the original and the Z-12 both carry `329X_V1.0.0`. That matches
the public AX3295B kids-camera teardown whose USB IDs and `BuildwinMedia-Player` string match this
family.

### Corrected: the X8D SPI reader address

`BLACKBOX_DUMP.md` and `firmware/README.md` listed the X8D reader as `0x02037404`. The signature
occurs exactly once, at file `0x039604`, so the correct address is **`0x02037004`**. The documented
value pointed at `0x039A04`, which does not disassemble as the reader at all. The dumped file's
sha256 matches what the document claims, so this was a stale address rather than a different build.
The Z-12's documented `0x02035A10` verifies correctly.

Worth noting: the dump tool locates the reader by scanning for the signature rather than by address,
so dumps were never wrong. Only the documentation was.

## Asset tables: the format changed in the newer build

| Build | Table base (file) | Table base (CPU) | Record | Records | Validated |
|---|---|---|---|---|---|
| Original KC02 | `0x0D3200` | `0x020D3200` | 8-byte `(rel_offset, size)`, 8-byte header | 97 | 84/97 point at an asset magic |
| MYSTILUCK X8D | `0x07FA00` | `0x0207D600` | 8-byte `(rel_offset, size)`, 8-byte header | 73 | 54/73 |
| HMOLLIMT Z-12 | `0x09FE00` | `0x0209D800` | **16-byte `(rel_offset, size, rel_offset+size, extra)`** | 47 | 41/47 |

The Z-12 is the important difference. Its first asset begins immediately after the table
(`0x0A00F0`), and every one of the 47 records satisfies `field0 + field1 == field2`, which is what
makes the wider record unambiguous. `tools/extract_assets.py` hardcodes the 8-byte form and
`TABLE_BASE = 0x0D3200`, so it will not read the Z-12 as-is.

The scanner used here is `/tmp/kc02-board-re/newbuilds/find_sfat.py`: it searches for the structural
signature and then scores candidates by how many records point at a JPEG/BMP/RIFF magic, which is
what separates the real table from the hundreds of coincidental word patterns. A candidate with no
magic validation should not be trusted - early attempts produced several.

## The Z-12 is a different product class, not just a newer KC02

Its strings include a credit/licensing and printer-control layer that the other two do not have:

* `OpenImage Credit Check Failed` / `OpenImage Credit Check Successed`, `OpenImage ID Check Failed`
* `Check Chip UUID Failed` / `Check Chip UUID Success`
* `Read IDBuf`, `Write KeyBuf`, `random data send ok`, `random write credit status failed`
* `open_printer_bluetooth_print_masf ret`, `MasfPrinterImageTIMER_REST_MASF failed`,
  `printer_gray_step_date_send start`
* `TEST ON`, `init uart0 finish`, `build time`

So the Z-12 has a **consumable credit system**, a **chip-UUID check**, and a **Bluetooth printer
path**. That changes what mapping it means: the interesting questions are the credit/ID check, the
UUID binding and the Bluetooth print channel, not only the camera UI. It is also the largest build
(fills 4 MiB, 66 complete JPEGs versus 22 in the original) and stores the update filename in UTF-16.

The X8D looks like a closer relative of the original (8-byte asset table, same family) plus a second
sensor; it contains `Digital Microscope` and a `Mar 13 2026` build date.

## Where the new code is

The propagated names double as a relocation map: each is an anchor pair (original address -> target
address) for a function unchanged between products. Where the gap between consecutive anchors is much
larger in the target than in the original, the extra bytes are code with no counterpart in the
original. `tools/sfat/new_regions.py` computes that and then characterises each region by which
*named* functions it calls - necessary because these regions contain no inline strings (the strings
live in the resource blob).

Roughly a third of the X8D and nearly half of the Z-12 is inserted code, and it is not spread thinly:

| Build | Inserted | Of the mapped application |
|---|---|---|
| MYSTILUCK X8D | 0x14BC8 bytes, 10 regions | 32 % |
| HMOLLIMT Z-12 | 0x1CBDC bytes, 11 regions | 45 % |

### MYSTILUCK X8D

| Region | Inserted | Unnamed | Signature |
|---|---|---|---|
| `0x0204FD48..0x0205C308` | 0xC180 | 99 | `memcpy`, malloc/free, `ax32xx_mjpegEncodeSizeSet`, `audio_overlap_add`, `close` - a media encode/mux block |
| `0x0200E934..0x020177FC` | 0x1274 | 187 | `win_find_child_by_id` x216, `winSetVisible`, `winSetbgColor` - **UI** |
| `0x02006C68..0x0200C4A0` | 0x1640 | 66 | `win_find_child_by_id` x154, `winSetVisible` x65 - **UI**, next to `mode_photo_tick` |
| `0x0201B334..0x0201EAB4` | 0x2860 | 44 | `asset_decode`, `ui_mode_request`, `XMsgQPost`, `game_random_byte` - **mode/UI** |
| `0x02047BF4..0x02049A60` | 0x1454 | 24 | `hal_iic08bitAddrWrite`, `hal_iic016bitAddrWrite`, `ax32xx_csiEnable`, `hal_gpioWrite` - **sensor** |
| `0x020394B8..0x0203A3E4` | 0xEFC | 13 | `hal_mjpegCallback_state`, `mjpeg_adapt_encode_quality`, `hal_audc_stamp_*` - MJPEG/audio |

### HMOLLIMT Z-12

| Region | Inserted | Unnamed | Signature |
|---|---|---|---|
| `0x020524FC..0x020693D4` | 0x168D8 | 228 | `memcpy` x47, `uart_Printf` x28, `display_get_mode`, `hal_spiFlashRead` - a **92 KB** new subsystem |
| `0x02049A5C..0x0204CC04` | 0x1B94 | 42 | `kc02_busy_delay` x31, `hal_gpioWrite` x28, `kc02_printer_helper_C80` x10, `ax32xx_adcEnable` - **printer** |
| `0x02045B58..0x02046F94` | 0xAF4 | 37 | `nv_read`, `hal_spiFlashRead`, `ascii_draw_get` - resource/UI helper |
| `0x0201CF30..0x0201E1A8` | 0xC68 | 38 | `win_find_child_by_id` x49, `winSetVisible` x19, `winSetStrInfor`, `ui_mode_request` - **UI**, right after `ui_grid_init` |
| `0x02047274..0x02048794` | 0xB08 | 22 | `hal_iic016bitAddrWrite`, `ax32xx_csiEnable`, `hal_gpioWrite` - **sensor** |
| `0x02037E44..0x02038700` | 0x88C | 9 | `hal_mjpegCallback_state`, `mjpeg_adapt_encode_quality` - MJPEG |

### What this already tells us

* **The second sensor is in the sensor blocks**, and they are the only new regions calling the
  bit-addressed I2C helpers alongside `ax32xx_csiEnable` and GPIO writes: X8D `0x02047BF4`, Z-12
  `0x02047274` and `0x02045B58`. Both builds replaced sensor handling next to
  `Sensor_DebugRegister`, which is exactly where a second sensor's probe and init would go.
* **The row-based menu is in the UI blocks.** The Z-12's sits immediately after an anchor named
  `ui_grid_init` and is dense with `win_find_child_by_id` / `winSetVisible` / `winSetStrInfor`, i.e.
  it drives existing sUI widgets rather than a new toolkit. The X8D has two UI blocks, and the one at
  `0x0200E934` calls `win_find_child_by_id` **216 times**, which reads like generated per-row code.
* **The Z-12's 92 KB block is its own library.** It contains the code that references the
  print-module vocabulary found earlier - image credit/ID checks, a chip UUID, key buffers, a
  Bluetooth printer path (`MasfPrinterImageTIMER_REST_MASF`). It is the single largest inserted
  region in either build. What that vocabulary *means* is a separate question, and the section below
  deliberately does not call it a user-facing licence scheme.
* Both builds also extend the **MJPEG/audio** path at the same place (next to `hal_mjpegStart`).

The I2C *addresses* of the second sensor did not fall out of argument scanning - the calls go through
function-pointer tables, so that needs the sensor ops structures read, not the call sites.

## The selfie sensor: found

Both new builds add a **sensor-ID dispatch** that the original does not have. Decompiled evidence is
in [`new_builds/evidence/`](new_builds/evidence/) (`x8d_sensor.txt`, `z12_sensor.txt`,
`orig_sensor.txt`).

```c
/* Z-12, FUN_ram_02047344 (2356 bytes) */
uVar2 = *(char *)(sensor_base + 0x50);
if (uVar2 == 0x48) return;                      /* the original front sensor */
if (uVar2 == 0xa0 || uVar2 == 0x9b) { ... }     /* new */
```

```c
/* X8D, FUN_ram_02047f4c (4476 bytes) */
uVar1 = *(char *)(sensor_base + 0x40);
if (uVar1 == 0xa0 || uVar1 == 0x9b) { ... }     /* new */
```

| Build | Function | Reads ID at | Handles |
|---|---|---|---|
| Original KC02 | none | - | - |
| MYSTILUCK X8D | `FUN_ram_02047f4c` (4476 B) | base `+0x40` | `0xA0`, `0x9B` |
| HMOLLIMT Z-12 | `FUN_ram_02047344` (2356 B) | base `+0x50` | `0x48` (no-op), `0xA0`, `0x9B` |

`0x48` is the value the original's own sensor item carries (`H63P`, item `+0x48`), so it is the
existing front sensor. **`0xA0` and `0x9B` are new**, and a search of the original's whole sensor area
finds no comparison against either - only unrelated `0x40`/`0x48` hits from GPIO setup. That is the
selfie sensor.

Two IDs rather than one means the code accepts either part; whether that is two revisions of the same
back sensor or a front/back pair cannot be told from the IDs alone.

Mechanism: both functions take a small `param_1` selector (1..5) and write a per-ID table of two-byte
`{register, value}` pairs through a helper (`FUN_ram_020479b4` in the X8D, `FUN_ram_02046c34` in the
Z-12). So the new sensor is supported by added register tables, not by a different driver path - the
sensor framework itself is unchanged. Both also call `ax32xx_csiEnable(0)` on entry, so they are
reconfiguring the CSI path as well as the sensor.

Also present in both: five contiguous 20-byte stub functions immediately after `Sensor_DebugRegister`
(`FUN_ram_02047C60..0x02047CB0` in the X8D), which look like unfilled slots in a sensor ops table
rather than real code.

## The row-based menu: found

The new menu is built **on the original's own grid framework**, not a new toolkit. It sits in the
region after the anchor `ui_grid_init` in the Z-12 (`0x0201CF30..0x0201E1A8`) and its calls are
`win_find_child_by_id`, `winSetVisible`, `winSetStrInfor`, `ui_mode_request` - all existing sUI
functions.

* `ui_grid_init` itself is a propagated name, so the original already had it: it clears a global,
  fills a 240-line buffer and calls `ui_grid_load_page`. The new menu reuses that page machinery.
* `FUN_ram_0201cf78` is the mode entry: it calls `ui_grid_init()` and then blocks in
  `XMsgQPend(...)`, i.e. a message loop, which is the classic mode-entry shape.
* Child widget IDs **5..9** act as the rows. `FUN_ram_0201d2f4` shows the pattern: for a window it
  walks `win_find_child_by_id(win, 6)` through `9`, sets text via `FUN_ram_02023580(widget, value)`
  and calls `winSetVisible(widget, 0/1)` to draw or hide each row.
* Six 24-byte functions in a row (`0x0201D264..0x0201D2DC`) all decompile to `return 0`. Uniform
  size and adjacency point at placeholder table slots; they should not be read as menu logic.

So the "new row-based menu" is a new screen composed from existing widgets, with rows as child
elements and a page loader - which is also why it leaves so little new vocabulary in the strings.

The X8D's equivalent is the block at `0x0200E934..0x020177FC`, which calls `win_find_child_by_id`
216 times, and a second at `0x02006C68..0x0200C4A0` with 154 such calls.

## The Z-12 has a real Bluetooth UART, and only the Z-12

Found while checking whether the "bluetooth" string meant anything. It does, and this is code rather
than vocabulary.

**Only the Z-12 touches the SoC's BT UART registers** (`BUCON` `0x9040`, `BUDATA` `0x9048`, `BUSTA`
`0x904C`). A scan for immediates materialising those SFR numbers finds:

| Build | BT-UART SFR accesses |
|---|---|
| Original KC02 | none |
| MYSTILUCK X8D | none |
| HMOLLIMT Z-12 | `0x9040` x1, `0x9048` x1, `0x904C` x2, at `0x02026B7C..0x02026D34` |

Decompiled, that is a small serial driver sitting between `ax32xx_uart0SendByte` and
`ax32xx_timerISRRegister`, i.e. in the HAL UART area:

* `FUN_ram_02026cb8(baud, callback)` - `BUCON = 0`, then
  **`BUBAUD = 144000000 / baud - 1`**, then `BUCON |= 0x10`, `BUCON |= 4`, then it stores the RX
  callback and enables an interrupt. That is baud-rate programming against a 144 MHz clock.
* `FUN_ram_02026b7c()` - the RX handler: masks `BUSTA` bit 3, reads `BUDATA` and calls the registered
  callback with the byte.
* `FUN_ram_02026bd8(enable)` - BT module power/enable through GPIO plus clock setup.

And it is actually used:

```c
/* FUN_ram_020365c8 - 300 bytes */
if (sRam0209d794 == 0) {
    FUN_ram_02026bd8(1, ...);                        /* module power/enable */
    FUN_ram_02026cb8(0x70800, FUN_ram_02035e24);     /* BT UART, 460800 baud */
    DAT_ram_0209d7f0 = FUN_ram_02035e5c;
    sRam0209d794 = 1;
}
iVar1 = FUN_ram_0201e5ec();
if (1999 < (iVar1 - iRam0209d790)) {                 /* every ~2000 ticks */
    iVar3 = hal_rtcTime2String(FUN_ram_02033f80());  /* current clock */
    for (uVar5 = 0; uVar5 < strlen(iVar3); ++uVar5) {
        FUN_ram_02026d2c(*(char *)(iVar3 + uVar5));  /* send each character */
    }
    FUN_ram_02026d2c(0xd); FUN_ram_02026d2c(10);     /* CR LF */
}
```

So the Z-12 opens the BT UART at 460800 baud and **periodically transmits the RTC time as ASCII with
CRLF**. A second function (`FUN_ram_020366f4`) performs the same two setup calls without the periodic
loop and is called from `FUN_ram_020595b4` in the print library. `FUN_ram_020365c8` has no direct
caller, so it is reached through a pointer.

### What this does and does not show

* **Does show:** the Z-12 firmware drives the BT-labelled UART with real baud programming, an RX
  callback path, interrupt enable and a periodic RTC transmit. `init uart0 finish` sits in the same
  area (`0x020364CC`), and the Z-12's own string calls it `open_printer_bluetooth_print_masf`.
* **Does not show:** that a radio is fitted, or that the link is wireless. There is a UART transport
  here, not a Bluetooth stack - a search for `hci`, `rfcomm`, `gatt`, `spp` and `advertis` finds
  nothing (the `hci` and `ble` hits are substrings inside binary data and `useDoubleBuff`). So the
  camera speaks a vendor protocol over a serial port; whether the far end is a Bluetooth module or a
  wired printer link is a hardware question. Look for a module and antenna on the board.

### This changes the reading of the credit/UUID vocabulary

If that serial link goes to an **external** print module - which is what
`open_printer_bluetooth_print_masf` and the `MasfPrinter*` names suggest - then the image
credit/ID checks, the chip UUID and the key buffers plausibly belong to the **printer module's
protocol**, checked as photos are sent to it, and not to the camera at all.

That fits the observation that nothing in the camera's UI or packaging mentions licensing: the
camera would be the client of a licensed printer or consumable, and the enforcement would be on the
other side of the serial link. It is a better fit for the evidence than a user-facing licence on the
camera, but it is still an interpretation - the decisive test is still to trace the Z-12's print entry
point and see whether a credit check gates it.

## What the Z-12's print-module vocabulary is (and is not)

An earlier revision of this document described the 92 KB block as "the credit and licence
subsystem". That was an over-claim and is retracted. What is actually established:

**The strings are unique to the Z-12.** `OpenImage Credit Check`, `Check Chip UUID`, `Read IDBuf` /
`Write KeyBuf`, `random write credit status failed`, `open_printer_bluetooth_print_masf`,
`MasfPrinterImageTIMER_REST_MASF`, `printer_gray_step_date_send` appear in the Z-12 and in neither
the original nor the X8D.

**They are data, and the code is elsewhere.** All of them sit at `0x0207xxxx`-`0x02086xxx`, in a span
of `0x020754F8..0x0209FE00` that contains only 34 discovered functions across 174 KB. That is a data
area, not code. Checking "are the strings inside the 92 KB block" is therefore the **wrong test** and
proves nothing either way; the strings of any build live well away from the code that uses them. The
right test is a cross-reference: scanning for `l.movhi`/`l.ori` pairs that materialise the string
addresses puts their users at `0x02052C38`, `0x020532F0`, `0x02053D38` and `0x02055F2C`, all inside
the 92 KB block.

**The region is a library, and it is heavily used.** Its functions are entered from 112 call sites
outside itself, including the app init area (`0x0200027C`) and camera-mode code (`0x02008C38`).

**The print paths look live, but the UUID check does not.** Callers of the individual functions:

| Function | Size | Callers | Callers outside the block |
|---|---|---|---|
| `FUN_ram_0205328c` printer gray-step | 1376 B | 1 | `0x0204A9F0` - inside the new printer region |
| `FUN_ram_02053b38` Bluetooth print | 608 B | 1 | `0x0204C0FC` - inside the new printer region |
| `FUN_ram_02052b0c` credit status | 548 B | 1 | `0x0203FA4C` |
| `FUN_ram_02055e94` chip UUID | 208 B | 2 | **none** - called only from within the block |

So the printer and credit calls come from the Z-12's **new printer code**
(`0x02049A5C..0x0204CC04`, the region whose callees are `kc02_busy_delay`, `hal_gpioWrite`,
`kc02_printer_helper_C80`, `ax32xx_adcEnable`), which makes them look live rather than dead. The chip
UUID check has no caller from outside the block at all, so that particular path may be internal-only
or unused.

**Interpretation, explicitly as an open question.** The shape - image credit and ID checks, a chip
UUID, key buffers, and a named third-party printer (`MASF`) with Bluetooth - is what module
authentication for a licensed or consumable print module looks like. It is also what a vendored
third-party print SDK looks like when a product only uses part of it. Both readings fit, and neither
implies that the owner sees a licence prompt: authentication between chip and print module is
invisible by design. Nothing in the UI or packaging would show it.

Settling it needs one of: the product's print entry point traced to see whether the credit check
gates printing, the `MASF`/`OpenImage` vocabulary matched to a named third-party SDK, or a print run
observed to fail on a non-genuine module. None of those has been done.

## What is not yet found

* **The selfie sensor.** No sensor part number appears in ASCII in either new build. The original
  carries `H63P` at file `0x0CDA24` (sensor item `+0x34`), but neither new build has an equivalent
  ASCII string, and the Z-12's only hit (`MZ265`) is inside JPEG data. Sensor identity therefore
  needs the code-level route used for the original: find the sensor resource item(s), the I2C
  addresses and the init tables. Two sensors should mean two items and two init paths.
* **The row-based menu.** Both builds share most UI vocabulary with the original, so the difference
  is structural rather than textual, and the UI text is not ASCII (the original keeps its labels in
  a compressed UI resource). This needs the mode/screen tables read in Ghidra, comparing against
  the original's `sUI`-based screens documented in
  [CAMERA_SCREEN.md](CAMERA_SCREEN.md) and [MAIN_MENU_SCREEN.md](MAIN_MENU_SCREEN.md).
* Whether the two new builds keep the same `cmd_ctx`/`xport_ctx` offsets (the dump doc says they do)
  and where their USB, printer and mode tables live.

## Next steps

1. **Import both images into Ghidra** as additional programs alongside the original (additive, with
   the shared project lock), using the same little-endian no-delay OpenRISC language, and let
   auto-analysis find functions. Everything else is easier afterwards.
2. Run the existing exporter scripts against the new programs to dump the candidate sensor items,
   the mode descriptors and the screen tables.
3. Fix `tools/extract_assets.py` to take a table base and record layout, so it can read all three
   builds and the Z-12's assets can be inspected (that is also the fastest way to find the row-based
   settings menu artwork and any extra sensor resources).
4. Only then attempt the two feature questions in depth.

Nothing here has touched flash or the cameras: this pass is entirely offline file analysis.
