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
| `ax3292-adc.h` | Measured ADC completion and synthetic sample policy |
| `ax3292-timer.h` | Measured TT counter enable/reset/readback |

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

## Hardware evidence

The original KC02 bench record is
[`docs/hw/2026-10-15.md`](../../../../docs/hw/2026-10-15.md), with a portable
fixture in `tests/fixtures/hardware/original-2026-10-15.json`.
`tests/test_emu_hardware.py` compiles the production ADC/TT helpers and Luma's
raw-counter conversion. Keep new measurements, backend fixes and regressions
in the same workstream; do not seed stock runtime snapshots as reset state.

Confirmed corrections: original JEDEC ID `5e4016`; ADC GO self-clears; TT requires
explicit start and returns raw ticks, approximately 128 MHz against stock XOS
time. TT frequency is an estimate, not an external clock calibration. Original
H63P I2C identity now has a backend regression. DE geometry/stride decoding
already agrees with the USB-idle measurements. Preview remains to be measured.

## Cache SPRs

The AX3292 keeps its data/instruction cache-control registers in SPR groups 3
and 4 (`0x1800`, `0x2000`), not in the upstream OpenRISC cache SPR layout. The
machine now backs those two addresses so cache-aware guest code, such as the
bring-up bootstrap's dirty-D-cache guard, reads what it wrote.

TCG memory and device DMA are coherent, so the model does not implement
cache-line invalidate/writeback semantics. The registers are a read/write
backing store with a `TODO(approximation)` marker; do not read them as proof
that cache behaviour is emulated.

## Device-side USB (in progress)

`-M ax3292,...,usb-host=on` presents a host on the device port. Default is
**off**, so boot/UI behaviour is unchanged (verified by `tools/emu/smoke.py`).

What the firmware actually tests, traced end to end:

| stage | code | what it reads |
|---|---|---|
| per-frame / 10-tick task | `usb_sense_task` `0x0200d2c0` → `tick_usb_charger` `0x0200c8d0` | `boardIoctrl(dev 0 = "usb", 0x20000002, &out)` |
| device ioctl | `board_usb_ioctrl` `0x02003208` (dev list `0x020c259c`, entry 0) | `ax32xx_WKI1Read()` |
| pin | `ax32xx_WKI1Read` `0x0202e41c` | **RTC control `0x88E0` bit 21** (WKUP1) |
| session detect | `hal_usbdCheck` `0x02031e60` | `USB20CON0` (`0x9840`) bits 30/31 |

Note: the `0x20000002` in the symbol notes is a **`boardIoctrl` command code, not
a memory address**; there is no register at that address. `usb-host=on` therefore
(a) sets RTC `0x88E0` bit 21 on read, and (b) asserts `USB20CON0` bits 30/31 on
write. With that, `usb_sense_state` (`0x020d416e`) leaves 0 and becomes 1.

The DUSB register file lives in `ax3292-usb.h` (74 SPRs, matched by explicit
lookup, never by range — a contiguous range hook swallowed `LCDSTA` and broke the
menu). Still missing for full enumeration: EP0 control-transfer DMA, raising
`IRQ_USB20` (15), and a host-side driver; the 1 -> 2 transition additionally needs
`dusb_ready()` and host BOT traffic, which is what unlocks UI mode 10.

### EP0 host injection (implemented, controller status still missing)

`-M ax3292,...,usb-fifo=/path/to/fifo` consumes a byte stream from a FIFO and
appends device responses to `<fifo>.out`:

| bytes | meaning |
|---|---|
| `'S'` + 8 | EP0 SETUP packet: copied to guest memory at `USB20E0ADR`, `CSR0` bit 0 (RxRdy) set, `IRQ_USB20` (15) raised |
| device -> host | whenever the guest writes `CSR0` with bit 1 (TxRdy), `USB20CON1 & 0x1fff` bytes are copied out of `USB20E0ADR` to `<fifo>.out` and TxRdy/DataEnd are retired |

Current blocker: with `usb-host=on` and the stack started, the firmware spins
reading `USB20_IntrUSB` (0xA028) and `USB20_IntrInl` (0xA008) — the controller
status the DUSB init/ISR waits on. The handler at `0x02031c2c` tests IntrUSB
**bit 2** first, then bits 0/1. Until the model reports the awaited cause, the
stack never finishes initialising: `usb_sense_state` stays 1 (it does reach 1),
`app_context.b2` stays 0, and injected SETUPs are never dispatched. Next step is
decoding the rest of that handler (or the caller that spins) and asserting the
right IntrUSB bits.
