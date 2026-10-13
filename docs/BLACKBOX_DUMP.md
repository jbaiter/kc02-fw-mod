# Black-box USB flash dump (newer KC02-class builds)

This documents the **MYSTILUCK X8D** and **HMOLLIMT Z-12** units encountered in
this project. They have the same USB identity as the original HiMont KC02
(`0219:3280`, mass-storage interface 4) and the same SoC class, but different
firmware builds: the original tool's fixed addresses do not match and the
camera reboots when they are called. Dumps are kept in
[`../firmware/`](../firmware/README.md).

There is **no firmware image** of this unit. It was dumped over USB anyway by
using a firmware-independent code-execution stage, then locating the firmware's
own SPI reader by signature.

## Result

| | |
|---|---|
| Device | MYSTILUCK X8D |
| Size | 4,194,304 bytes (4 MiB) |
| SHA-256 | `3ed8c0f145708814397cf675101ee3ff6b8e280c4b09742af2e043c4cc1bc42e` |
| MD5 | `8e0636e16d4a54636d75f879f71532c8` |
| Reads | Two complete reads, byte-identical |
| Markers | `BLDR` @ 0x04, `0x55AA` @ 0x1FE, `SFAT` @ 0x304 |
| Content | ends near `0x1CF000`; `0x1CF000..0x3FFFFF` is `0xFF` (erased) |
| Assets | 41 JPEG SOI, 21 BMP, 12 RIFF; sampled JPEGs decode |

The 8-bit boot-header additive checksum is **185**, not `0`. The original build
dumps sum to `0`. This build ships with a non-zero value and boots, so the
field is evidently not enforced at boot. The image is byte-stable across reads,
so this is not a read artifact.

## Delivery: version-independent code execution

Every AX3292 KC02 keeps the USB endpoint buffers at fixed low-SRAM addresses:

* `0x8040` — IN/TX buffer (512 bytes)
* `0x8250` — OUT/RX buffer (512 bytes)

The stock 0xCD vendor command calls a host-supplied function pointer
(`CDB[1..4]`) with `r3 = &cmd_ctx`, `r4 = &xport_ctx`, after storing the CBW
transfer length in `cmd_ctx+0x24`. That gives code execution with no
firmware-specific address:

1. Stage a 512-byte blob via an ordinary SCSI `WRITE(10)` to LBA 1 (the unused
   gap between the MBR and the first partition). The host data lands in the
   fixed `0x8250` RX buffer. The payload lives at offset `0x40` (`0x8290`) so
   the next command's 31-byte CBW cannot clobber it.
2. Send vendor `0xCD` with `Func1 = 0x8290`, `DataAddr = <address>`,
   `Func2 = 0xFFFFFFFF` (skip callback).
3. The staged payload copies `<address>` through `0x8040` and streams it with
   the firmware's own bulk-TX pointer. Copying matters: the USB DMA only
   decodes a short address window, so sending directly from SDRAM aliases into
   low SRAM.

`cmd_ctx` / `xport_ctx` field offsets are the same on both builds:
`cmd_ctx+0x24` length, `cmd_ctx+0x38` address, `xport_ctx+1` endpoint,
`xport_ctx+0x24` `fp_usb_bulk_tx`.

The staging sector is saved and restored, so the SD card is left unchanged.

## Located addresses (this build)

| Symbol | Address | Notes |
|---|---|---|
| `cmd_ctx` | `0x02085E14` | `MscCmd`; referenced as `0x2085E14` |
| `cb_mem_rwex` | `0x020414E8` | memory read/write dispatcher |
| `cb_mem_read` | `0x020413F0` | memory -> USB |
| `cb_mem_write` | `0x02041468` | USB -> memory |
| SPI flash reader | `0x02037004` | `spi_read(offset, buffer, count)` |

The SPI reader is located automatically by scanning the loaded application for

```
fc 4f e1 d7 ec 17 e1 d7 f0 77 e1 d7 f4 97 e1 d7
e8 0f e1 d7 00 00 43 aa f8 a7 e1 d7
```

which is unique in the image. The original build's reader at `0x0203D38C` has
the same body, so one signature covers both.

## Other builds dumped with the same method

The signature-based SPI-reader discovery is firmware-independent; no per-camera
address is needed to dump. Located so far:

| Device | SPI flash reader | Dump SHA-256 |
|---|---|---|
| Original HiMont KC02 | `0x0203D38C` | `218c6cdf…` |
| MYSTILUCK X8D | `0x02037004` | `3ed8c0f1…` |
| HMOLLIMT Z-12 | `0x02035A10` | `10da5dbf…` |

All three have the identical SPI-reader body and the same fixed USB buffers, so
`tools/dump_firmware.py --profile blackbox` works for each without changes. See
[`../firmware/README.md`](../firmware/README.md) for the build comparison.

## Tooling

`tools/dump_firmware.py` now carries both recipes and selects one automatically
(`--profile auto`):

* `blackbox` (default) — the staging technique above; needs no firmware-specific
  delivery address and locates the SPI reader at run time.
* `stock` — the original RAM-shim recipe for the first build.

Payload sources live in `tools/payloads/`.
