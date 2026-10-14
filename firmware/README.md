# Firmware dumps

Local, per-unit backups for comparison. These binaries are **not distributed**
(see the root `.gitignore`: `firmware/*.bin`). Dump your own camera with
`tools/dump_firmware.py`; the recipe is auto-selected, or force one with
`--profile blackbox` / `--profile stock`.

| File | Device | Size | SHA-256 |
|---|---|---|---|
| `original.bin` | HiMont KC02 (original unit) | 4 MiB | `218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5` |
| `mystiluck-x8d-8e0636e1.bin` | MYSTILUCK X8D | 4 MiB | `3ed8c0f145708814397cf675101ee3ff6b8e280c4b09742af2e043c4cc1bc42e` |
| `hmollimt-z12-10da5dbf.bin` | HMOLLIMT Z-12 | 4 MiB | `10da5dbf24e3e67a7133d22cca2252a4e9f3452fdc7dd35ac9c7869bbd6a00ee` |
| `usb-2k.bin`, `usb-2k-timed.bin`, `usb-512-benchmark.bin` | original KC02 | 4 MiB | `218c6cdf…` (byte-identical benchmarks) |

All three are little-endian OpenRISC images of the same Buildwin AX329x family, and the
X8D self-identifies as `Ax3295B-V3.0` rather than `AX3292`. All three carry `BLDR` @ `0x04`, `0x55AA` @ `0x1FE`
and `SFAT` @ `0x304`, one mass-storage interface (4), and the same
`cmd_ctx`/`xport_ctx` layout. They differ in application build and assets.

## Build comparison

| | Original KC02 | MYSTILUCK X8D | HMOLLIMT Z-12 |
|---|---|---|---|
| Version word @0x08 | `00010500` | `00010500` | `00010510` |
| Header checksum | `0` | `185` | `53` |
| Last non-`0xFF` | `0x3071FF` | `0x1CEFFF` | `0x3FFEFF` (fills 4 MiB) |
| Non-`0xFF` bytes | 3.17 MB | 1.90 MB | 3.77 MB |
| `l.jr r9` functions | ≈2632 | ≈2169 | ≈2420 |
| JPEG SOI markers | 27 | 41 | 87 |
| SPI flash reader | `0x0203D38C` | `0x02037404` | `0x02035A10` |
| Update filename | ASCII `DestBin.bin` | ASCII `DestBin.bin` | **UTF-16** `DestBin.bin` |
| Platform strings | `329X_V1.0.0`, `JRX_20220309` | — | `329X_V1.0.0`, `JRX_20220309` |

The Z-12 is the largest build (uses essentially the whole flash, twice the JPEG
assets of the original) and, like the X8D, has a non-zero header checksum and a
relocated application. It shares the original's `329X`/`JRX_2022` platform
strings but encodes the update filename in UTF-16.

The SPI reader has byte-identical code in all three builds and is located
automatically by signature. Technique and located addresses:
[`../docs/BLACKBOX_DUMP.md`](../docs/BLACKBOX_DUMP.md); first-pass map of the two
newer builds (mapping, asset tables, what is still unmapped):
[`../docs/NEW_BUILDS.md`](../docs/NEW_BUILDS.md).
