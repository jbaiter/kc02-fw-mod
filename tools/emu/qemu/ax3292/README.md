# AX3292 machine sources

The repository owns the AX3292 machine and its device models. `build-qemu.sh`
copies these files into `hw/or1k/` of a QEMU source tree; `0001-ax3292-machine.patch`
carries only the integration changes (`target/or1k/*`, `hw/or1k/Kconfig`,
`hw/or1k/meson.build`).

Keeping them here rather than inside the patch means the model is ordinary
readable source: `rg` finds it, code review sees it, and the tests compile the
real files instead of reconstructing them from diff text.

| File | Contents |
|---|---|
| `ax3292.c` | Machine, SPR bus, boot handoff, and the devices that need machine-level state |
| `ax3292-printer.h` | GPIO thermal head and platen |
| `ax3292-jpeg.h` | JPGA encode and JPGB decode |
| `ax3292-display.h` | DE scanout, OSD and keypad input |
| `ax3292-sensor.h` | H63P/BF3A03 sensors and CSI DMA |
| `ax3292-sd.h` | SD command controller and sector DMA |

The `-*.h` files are included into `ax3292.c` on purpose. They are one machine
with shared state, and the split is by device rather than by translation unit:
the printer is self-contained enough to be compiled and tested on its own
(`tests/test_emu_printer.py` does exactly that), while the CSI and JPEG paths
share the machine's register backing store directly.

Run arguments are machine properties, not environment variables:

```sh
qemu-system-or1k -M ax3292,flash=original.bin,sensor=on,sd-image=card.img
```

Diagnostics stay in the environment and are inert unless `AX3292_DEBUG=1`.
See [../README.md](../README.md) for the full list.

Each known approximation carries a `TODO(approximation)` marker, so
`rg 'TODO\(approximation\)' tools/emu/qemu/ax3292` is a truthful debt list.
