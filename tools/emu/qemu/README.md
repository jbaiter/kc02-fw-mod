# AX3292 QEMU emulator

The patched QEMU runs `firmware/original.bin` and displays its colour menu and
camera UI. Arrow keys reach the firmware's ADC keypad scanner. Enter opens the
selected item; Escape returns to the menu. A fake H63P sensor supplies moving
colour bars through CSI DMA.

We verified three cold headless runs through menu selection, camera preview,
motion and return to the menu. We also verified the native SDL window. Screenshots:
[main menu](../../../docs/images/emulator/menu.png),
[camera preview](../../../docs/images/emulator/camera.png).

## Run an interactive session

```sh
python3 tools/emu/run.py firmware/original.bin --interactive
```

Use short presses, around 200 ms. The firmware distinguishes long presses and
may ignore them for menu navigation. Click the QEMU window to give it keyboard
focus. Close the window to quit.

| Host key | Camera input |
|---|---|
| Arrow keys | Up, Down, Left, Right |
| Enter | OK |
| Escape | Back / power key; a long hold can shut down the guest |

The launcher enables the fake sensor in interactive mode. Pass `--no-sensor` to
omit it, `--trace` to enable SPR logging, or `--qemu /path/to/qemu-system-or1k` to
select another patched binary. It saves UART output beside the trace log.

## Build QEMU

Install the development packages for libjpeg, libpng and SDL2, plus QEMU's usual
build dependencies. On Debian, the additional packages are `libjpeg-dev`,
`libpng-dev` and `libsdl2-dev`.

```sh
QEMU_DISPLAY=sdl tools/emu/qemu/build-qemu.sh
```

The script uses QEMU 11.1.2. `QEMU_DISPLAY=none` builds a headless emulator;
`auto` uses SDL when its development files are available. GTK and OpenGL are not
required. You can choose a fresh source directory:

```sh
QEMU_SRC="$HOME/.src/qemu-ax3292" QEMU_DISPLAY=sdl tools/emu/qemu/build-qemu.sh
```

The patch marker records a checksum. If an existing tree has an older patch, the
script stops rather than mixing revisions. Use a fresh directory; do not remove
the marker and apply the new patch over modified sources.

## Attach a writable SD card

```sh
python3 tools/emu/create_sd.py /tmp/kc02-sd.img --size-mib 64
python3 tools/emu/run.py firmware/original.bin --interactive \
  --sd-card /tmp/kc02-sd.img --gdb 1234
```

To test the **Foto** viewer, add `--test-images` to the creator command. This
requires Pillow and seeds four baseline 4:2:0 JPEGs in
`PHOTO/PHO00001.JPG` through `PHOTO/PHO00004.JPG`: colour bars, an RGB gradient,
orientation markers and a portrait image. KC02 uses the `PHO` prefix and five
numeric digits (`managerFileNameCreater` / `name2string`, verified in Ghidra).
Select **Foto**, use **Up/Down** to browse, and **Escape** to return. **Right**
opens the delete menu. Stop the emulator before changing its mounted image.

Run the viewer regression on this card with:

```sh
python3 tools/emu/smoke.py firmware/original.bin \
  --viewer-card /tmp/kc02-sd.img --output /tmp/kc02-viewer-check
```

Verified viewer screenshots:
[colour bars](../../../docs/images/emulator/viewer-bars.png),
[orientation](../../../docs/images/emulator/viewer-orientation.png).

The creator makes a sparse raw image with an MBR and FAT16 partition. It refuses
to overwrite an existing file. The controller opens an existing regular file
read/write and locks it against a second emulator. The firmware discovers the
card through SD commands, reads its capacity from CSD, mounts FAT16 and writes
sectors through DMA. We do not inject card state into application RAM.

We verified persisted `VIDEO`, `PHOTO`, `MP3` directories and `VIDEO.LST` after a
cold boot. Reads/writes are synchronous and flush the host stream. Optional
`AX3292_SD_TRACE=1` logs SD commands and sector transfers. Normal-speed SDHC is
modelled; CMD6 returns a 64-byte switch-status packet without high-speed support.
The backing image size must be a multiple of 512 KiB. If a launch exits with a
card-lock error, close the other emulator using that image. The launcher prints
QEMU errors and returns its exit status.

### Take and verify a photo

Open **Kamera** and press **Enter** for the shutter. The firmware encodes the
fake sensor, saves a photo through SD DMA, and returns to the moving preview.
Open **Foto** to view it. Two captures and a third cold, viewer-only boot passed;
the older photo remained byte-for-byte unchanged.

```sh
python3 tools/emu/capture_smoke.py firmware/original.bin \
  --output /tmp/kc02-capture-check
```

This creates its own card and requires Pillow. Each run saves screenshots,
unmodified photo files, independently decoded JPEG packets and a JSON report.
Use a new output directory for each test.

KC02 saves a **JRX wrapper**, not a plain standalone JPEG: a stitched preview
without EOI, the original JPEG, padding, and a final 12-byte APP9/JRX record with
a big-endian original-image offset. Ordinary JPEG readers can reject the whole
wrapper. `sd_read.jpeg_packets()` separates the original for host tools and adds
EOI to a copy of the preview for validation. It does not modify the SD image.
Stop the guest before inspecting its FAT with `sd_read.FAT16`.

## Export thermal-printer output

Create a card once, then launch with a printer output directory:

```sh
python3 tools/emu/create_sd.py /tmp/kc02-print-sd.img
python3 tools/emu/run.py firmware/original.bin --interactive \
  --sd-card /tmp/kc02-print-sd.img --printer-output /tmp/kc02-prints --gdb 1234
```

Close any emulator using that card before launching another. In **Kamera**, press
**Enter** to capture, save and print with the stock defaults. Wait for the moving
preview to return. The backend saves completed jobs as `print-00001.png`, etc.;
it preserves existing files and publishes each PNG after writing it in full.
Select **Einstellungen → Attribute drucken** to choose **Punktmatrix** (dither)
or **Graustufen**. The separate **Drucken** setting enables/disables printing.
Pass `--printer-no-paper` to simulate an empty/open printer.

The backend records the firmware's serial DATA/CLK, LATCH and STROBE signals.
The firmware performs grayscale conversion and dithering; the emulator applies
neither algorithm to the source JPEG. It retains the unsent shift-register bit
when the stock driver clocks only 383 of the 384 dots. It tracks motor phases and
includes blank feed in the PNG. PNG gray values represent normalized strobe duty;
thermal spreading, pulse-duration/energy calibration, temperature and paper
chemistry need physical-camera validation. The platen model assumes four motor
steps per raster row. Supply-off ends a job; interrupted jobs may have no PNG.

Verified examples: [Punktmatrix](../../../docs/images/emulator/print-dither.png),
[Graustufen](../../../docs/images/emulator/print-grayscale.png).

```sh
python3 tools/emu/printer_smoke.py firmware/original.bin \
  --output /tmp/kc02-printer-check
```

This Pillow-dependent test creates a private card and checks three cold boots:
dither, grayscale selected through real settings keys, and no paper. Each boot
also verifies capture, viewing and return to the menu, with prior photos intact.
Reports include PNG hashes, GPIO counts and screenshots. Use a new output path.
Without `--printer-output`, the backend does not override the paper input.

## Attach GDB

For a new session, enable a localhost GDB server:

```sh
python3 tools/emu/run.py firmware/original.bin --interactive --gdb 1234
# In another terminal:
tools/emu/gdb.sh 1234
```

The wrapper uses `gdb-multiarch` (the host's x86-only GDB cannot debug OpenRISC).
Connecting pauses QEMU. `continue` resumes it, Ctrl-C stops it, and `detach`
releases the running guest. For an existing session with QMP, the monitor command
`gdbserver tcp:127.0.0.1:1234` enables the server without restarting QEMU.

```gdb
info registers pc r1 r9
hbreak *0x020079b4
continue
# Later:
detach
quit
```

`0x020079b4` is the original KC02 UI key dispatcher. Prefer hardware breakpoints
so debugging does not replace guest instructions. This build's upstream OR1K
GDB stub serialises registers big-endian, despite little-endian guest memory.
The wrapper selects big-endian to decode PC/GPRs correctly; use `x/16bx ADDRESS`
for raw memory. Native word reads and instruction disassembly use the wrong
endianness in this configuration. Use Ghidra for firmware disassembly. The raw
application image has no ELF symbols, so the initial `??` is expected.

## Verify the UI without a desktop

```sh
python3 tools/emu/smoke.py firmware/original.bin \
  --output /tmp/kc02-smoke --runs 3
```

The harness starts its own headless QEMU processes and uses QMP keypad events:

1. Wait for the automatic menu and six decoded icon buffers.
2. Press Right and Left, checking the firmware's selection byte through
   read-only memory snapshots.
3. Press OK and wait for the colour-bar viewfinder.
4. Save two frames and check that the sensor marker moves.
5. Press Escape and check the return to the menu.

Each run saves five RGB PPM screenshots, a QEMU log and `report.json`. The test is
specific to the original KC02 dump and checks its SHA-256 before starting. It
neither writes application state nor modifies firmware instructions.

## Boot handoff and CPU changes

The original profile slices flash offsets `0x2600..0xd3200`, loads the application
at `0x02000000`, and enters `0x0200046c` with SP `0x027ffffc`. Alternate profiles
supply their own bounds, entry, SP and calibration address in `profiles.py`. The machine loads
low SRAM from flash offset `0x200`, uses 16-byte exception-vector slots with zero
bias. AX3292 SR bit 14 selects SRAM vectors (`intvct_sram/intvct_mask` in
`reset.S`), rather than upstream OR1K's high-address EPH. Interrupt entry uses
the low EVBA even when that bit is set. This matters for X8D's relative ISR jumps.

We skip ROM/SDRAM calibration. The machine supplies the SDK's nominal 800 mV
bandgap calibration at SRAM `0x222c`, which the boot ADC routine uses. This is
explicit boot-handoff data. We removed the earlier `AX3292_CALLFIX` instruction
rewrite and `AX3292_SDCARD` application-state injection.

The CPU changes support no delay slots, little-endian instruction/data accesses,
the chip's SPR numbering and a machine SPR hook. HP interrupt entry preserves
r2–r13 and odd r15–r31 in a hardware shadow bank; `rfe` restores them. The SDK's HP
wrapper relies on this and saves only r9/EPCR/ESR in software. LP uses SR bit 1;
HP uses bit 2 and can preempt LP. PICPR (`0x4804`) routes pending sources to
separate HP and LP vectors. Rotate IRQ23 belongs to LP.

Earlier UART byte counts and "four-byte call mismatch", "first-frame queue
bootstrap" and "lost interrupt race" diagnoses did not establish the failure.
Do not reuse those fixes from historical notes. UART length alone does not prove
that the UI is running.

## Peripheral model

| Device | Model |
|---|---|
| SPI0 | Read-only flash backing; byte commands and DMA |
| TMR0 / PIC / RTC | Periodic events, priority routing and acknowledgements |
| ADC | Busy bit clears; keypad ladder and healthy reference/supply samples |
| DAC | Buffer-empty completion lets firmware finish its startup sound; no host audio |
| MCP0–2 | Bounded memory copies, completion bit and W1C acknowledgement |
| Rotate | Counterclockwise YUV420 rotation; interleaved chroma pairs; busy clears synchronously, IRQ follows |
| LCD | Vsync/buffer-commit event bit 3 and shifted W1C acknowledgements |
| DE | Update handshake, programmed strides/geometry, YUV420 video and RGB565/A5 palette OSD |
| JPGA | Baseline 4:2:0 encode from guest YUV buffers, guest quantization/Huffman tables, bounded output DMA and IRQ16; row restart markers support the firmware's stitching |
| JPGB | libjpeg-backed baseline decode and scaling into guest YUV buffers; isolates JRX scans in host memory, defers completion by at least 1 ms, and respects IRQ masks |
| I2C / sensors | H63P ID `0x0b = 0x48`, or BF3A03 ID `0xfc = 0x3a`; optional PE11-selected dual banks, distinct patterns and BF3A03 mirroring |
| CSI | Fake frames written into programmed LCD and full-frame JPEG input buffers; frame-end IRQ14; line-scaler configuration kick self-clears even with CSI stopped |
| SD | Optional writable raw SDHC image; command/R2 responses, normal-speed switch status, sector DMA and CRC/completion flags. No card when no image is attached |
| Printer | Optional GPIO head shift/latch/strobe and platen model, active-low paper/cover input, bounded 384×8192 raster and exclusive numbered PNG jobs |

The panel's scanout is 240×320, mounted 90 degrees clockwise, so QEMU displays
320×240. The old 640×480 grid of rotate Y/UV buffers was not a composed screen.
The current scanout includes firmware text overlays and palette alpha.

Timer callbacks queue device work onto the owning vCPU between guest execution
blocks. This prevents concurrent DMA/PIC mutations. The IRQ helper acquires BQL
when needed. A main-thread bottom half uploads the resulting RGB frame to QEMU's
display console; calling SDL updates from the vCPU caused the black-window bug.

## Diagnostics

| Environment variable | Effect |
|---|---|
| `AX3292_FLASH` | Flash image path; the launcher sets it |
| `AX3292_SENSOR=1` | Enable synthetic camera input |
| `AX3292_SENSOR_3A=1` / `AX3292_DUAL_SENSOR=1` | BF3A03 and optional PE11-selected second camera |
| `AX3292_FLASH_ID` / `AX3292_FLASH_UID` | JEDEC ID (6 hex digits) and SPI UID (16 hex digits); profiles set these |
| `AX3292_PANEL_ROTATION` | Physical panel mount: 90 or 270 degrees |
| `AX3292_KEY_CHANNEL` / `AX3292_KEY_VALUES` / `AX3292_EXTRA_KEYS` | ADC channel, six base and five extra key values; profiles set these |
| `AX3292_KEY_TRACE=1` | Log pressed-key ADC samples |
| `AX3292_SPR_TRACE_MIN` / `AX3292_SPR_TRACE_MAX` | Inclusive SPR trace range |
| `AX3292_SD_IMAGE=/path/sd.img` | Writable raw SD image; the launcher sets this with `--sd-card` |
| `AX3292_SD_TRACE=1` | Log card commands and sector DMA |
| `AX3292_JPEG_TRACE=1` | Log codec geometry, completion and failures |
| `AX3292_PRINTER_DIR=/path/prints` | Enable printer GPIO capture and report paper/cover present |
| `AX3292_PRINTER_NO_PAPER=1` | With a printer directory, report missing paper/open cover |
| `AX3292_FB=/tmp/screen.ppm` | Atomically replace an RGB P6 scanout capture |
| `AX3292_FB_EVERY=5` | Capture interval in 10 ms ticks; minimum 5 |
| `AX3292_KEYFILE=/tmp/key` | Optional numeric ADC input; overrides native keyboard input |
| `AX3292_SPR_TRACE=1` | Enable register trace; default off |
| `AX3292_SPR_TRACE_LIMIT=300000` | Bound register logging |
| `AX3292_PC_SAMPLE=1` | PC/GPR sampler; also enable SPR tracing |
| `AX3292_PROBE=addr,...` | Up to eight hexadecimal RAM addresses, change-only samples |
| `AX3292_RAMDUMP=/tmp/ram.bin` | Snapshot SDRAM `0x02000000..0x027fffff` every 100 ticks |
| `AX3292_SFRDUMP=/tmp/sfr.bin` | Snapshot 64 KiB register backing store every 100 ticks |

`--qmp unix:/tmp/kc02.qmp,server=on,wait=off` exposes a local QMP socket for
read-only inspection and keypad automation. Prefer owned, bounded processes for
tests; do not use broad `pkill` commands while someone is using the emulator.

## Limits

This is a functional model, not cycle-accurate silicon. The original KC02 build
has the complete capture/viewer/printer regression. [Alternate-build status and
controls](../README.md#alternate-builds) distinguish UI/selfie checks from the
remaining Z-12 post-capture and alternate-printer work. We have verified FAT16
mounting, metadata writes, photo capture and viewing across cold boots. Video
recording, short CSI streaming line rings, USB device behaviour, full ISP
processing and audible sound remain unsupported or unverified. JPGA supports
contiguous output DMA and plain/`00dc` framing, not circular-buffer streaming.
The JPEG model requires the header to remain in the guest input buffer and uses
approximate chroma resampling. Without an attached SD image, the no-card state
is intentional.
