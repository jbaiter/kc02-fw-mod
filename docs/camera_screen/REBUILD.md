# Build the reconstructed camera screen

## Scope

`camera_screen_model.c` now implements all **33 camera-region entry points**:
mode enter/exit/tick, widget updates, every camera key/message handler,
countdown/burst capture, file saving and print orchestration. The earlier
partial model is preserved as `camera_screen_sketch.c`.

This module calls the **stock firmware** for `sUI`, sensor/LCD/MJPEG engines,
filesystem, audio, effects and printer control. It also uses stock resources
and RAM globals. It is not a standalone firmware image or recovered vendor
source. You do not need the full SDK to compile this module; you need the
OpenRISC compiler and the matching stock firmware image.

## Offline build

From the repository root:

```sh
python3 docs/camera_screen/build_camera_screen.py \
  --tool-prefix /tmp/ax3292-toolchain/prefix/bin/or1k-elf- \
  --firmware firmware/original.bin \
  --base 0x02300000 \
  --out /tmp/kc02-camera-build
```

**`0x02300000` is an example link address, not reserved or verified free RAM.**
Do not upload this build to that address. The stock heap may occupy it.
The build script does not access USB, patch a firmware image or install hooks.
The flat binary has no USB-callback shim; its first byte is not necessarily the
mode-enter entry point. Use the symbol map when designing later integration.

The script checks the firmware SHA-256, then compiles with GCC 4.9.1,
`-mno-delay -mhard-div -Os`, freestanding C11 and warnings as errors. It links
against the fixed-address imports in `firmware_imports.def`.

Outputs:

| File | Purpose |
|---|---|
| `camera_screen.o` | OpenRISC relocatable object |
| `camera_screen.elf` | Linked ELF32, little-endian OpenRISC, no-delay flag |
| `camera_screen.bin` | Flat module bytes, **not** an SD update image |
| `camera_screen.ld`, `.map`, `.nm`, `.dis` | Link inputs, symbols and disassembly |
| `camera_widgets.inc` | Generated writable 48-byte widget records |
| `manifest.json` | Source/binary hashes, 33 replacement addresses, stock imports and hook candidates |

The saved local build under [`build/`](build/) is **10,768 bytes**:
9,768 text/constant bytes, 976 data bytes, no BSS and 24 alignment bytes.
It exceeds the current RAM harness's **5,632-byte code slot**. Do not load it
with that harness or raise the limit without reserving and auditing memory.

The build checks:

- all 33 exported replacement symbols and no undefined symbols;
- all 960 widget-table bytes against the image, including the terminator;
- all 15 message bindings point to the new handlers, followed by the terminator;
- no direct `l.jal` from the new module into the original camera region;
- the original bytes at the three candidate mode-callback slots.

The widget table keeps the original constructor addresses, resource IDs,
geometry, padding and opaque extension bytes. Stock `win_open` mutates its
root private field and the new descriptor's handle, so these tables remain
writable. Do not substitute the SDK's 40-byte widget type.

## Host tests

```sh
python3 docs/camera_screen/test_camera_screen.py \
  --firmware firmware/original.bin \
  --listing docs/camera_screen/raw/disassembly.txt \
  --out /tmp/kc02-camera-tests
```

The current result is **6,018 passing assertions**, including **5,956
differential cases** for UP, DOWN, LEFT, RIGHT and BACK. The bounded instruction
interpreter re-encodes the original listing and checks each instruction against
the hash-pinned firmware before comparing controller-owned RAM with the C code.
It uses no-delay execution. Both sides mock calls into stock drivers/helpers.

The other tests cover lifecycle calls, preview handoff/effect selection, both
preset cycles, zoom limits, icons/text/dialogs, countdown/burst counts,
file accounting and cleanup, file-backed printing and memory-backed printing.
Allocation, stream-timeout and decode-failure tests check cleanup. The host
build runs with the undefined-behavior sanitizer.

[`test_results.json`](test_results.json) records the command and limitations.
These tests do not validate the target calling convention, peripheral behavior,
image output, print quality or on-device execution.

## Reconstructed contracts and deliberate differences

`firmware_imports.def` records call-site contracts rather than trusting imported
SDK prototypes. Additional raw decompilation and disassembly are in [`abi/`](abi/).
Important corrections include:

- File opener `0x02008550`: r3 is ignored; r4 receives the filename-output pointer.
- Encoded-frame getter `0x020416CC`: retain ignored r3; outputs occupy r4..r7.
- Resource setter `0x0202990C`: pass the window handle and resource value.
- Encoder start `0x02040C6C`: retain six arguments, including the r8 control.
- File encoder `0x02076F98`: retain the register and stack arguments.
- `win_open`: declare it variadic so GCC places the popup arguments on the stack.
- BACK: use the actual r4/r5 event/flag guard, not its misleading raw prototype.

The reconstruction is not byte-identical:

- Zoom/count strings include explicit null terminators; decoder-request padding
  and stream-output locals start initialized.
- Allocation, timeout, oversized-stream and decode failures return with cleanup
  rather than consuming uninitialized outputs. New output pointers/lengths start
  at zero.
- A terminal countdown value of zero hides the number and skips its audio tick,
  instead of depending on a stale/uninitialized icon register.
- UART diagnostic prints are omitted.

The memory encoder preserves the stock second-stage release-before-copy order.
The mocked stream does not prove the real buffer stays valid after release;
review that ownership contract before executing the module.

## Before installing a replacement

The manifest lists **candidate**, unapplied mode-3 hook slots:

| Slot | Stock target | Replacement |
|---|---|---|
| `0x020C2ADC` | `0x020082F4` | `camera_mode_enter` |
| `0x020C2AE0` | `0x020083C8` | `camera_mode_exit` |
| `0x020C2AE4` | `0x020084A0` | `camera_mode_tick` |

First repair power, reserve a module allocation, validate import/callback ABI
and ownership, and plan cache maintenance and rollback. Install hooks from a
non-camera mode; the new descriptor has no window until its enter callback runs.
Keep the stock scheduler, dispatcher and CSI/LCD callback.

Do not erase the old camera region. An existing call at `0x0200E1C8` uses the
shared file-opening helper at `0x02008550` outside camera mode. The rebuilt
camera itself uses its local replacement. Retained stock libraries may have
further indirect dependencies; the direct-call check is not a whole-program
proof.

No hardware execution, RAM placement or persistent deployment has been tested.
The build manifest keeps `deployment_allowed`, `placement_verified` and
`on_device_tested` false.
