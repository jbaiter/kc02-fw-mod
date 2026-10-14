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

Install the development packages for libjpeg and SDL2, plus QEMU's usual build
dependencies. On Debian, the additional packages are `libjpeg-dev` and
`libsdl2-dev`.

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

The launcher slices the application from flash offsets `0x2600..0xd3200`, loads
it at `0x02000000`, and enters `0x0200046c` with SP `0x027ffffc`. The machine loads
low SRAM from flash offset `0x200`, uses 16-byte exception-vector slots with zero
bias, and aliases SRAM at `0xf0000000` for SR[EPH].

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
| JPGB | libjpeg-backed baseline decode and nearest-neighbour scaling into guest YUV buffers |
| I2C / H63P | Sensor register transactions and read-only chip ID `0x0b = 0x48` |
| CSI | Fake frames written only into programmed DMA buffers at configured preview dimensions; frame-end IRQ14 |
| SD | No attached card; command response reads do not echo command arguments |

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
| `AX3292_SENSOR=1` | Fake H63P sensor |
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

This is a functional model, not cycle-accurate silicon. We have verified the
original KC02 build, not the other dumped cameras. We have not verified SD
recording, JPEG encoding, USB device behaviour, full ISP processing or audible
sound. The JPEG model requires the header to remain in the guest input buffer
and uses approximate chroma resampling. The no-card state is intentional.
