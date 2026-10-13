# Black-box dump payloads

`tools/dump_firmware.py --profile blackbox` ships two tiny freestanding OpenRISC
payloads as hex constants. These are their sources; they are assembled with the
reconstructed AX3292 toolchain (`or1k-elf-as` / `or1k-elf-objcopy`).

| Source | Purpose |
|---|---|
| `blackbox_read.s` | Copy CPU memory through the fixed TX endpoint buffer and stream it to the host (firmware-independent arbitrary memory read). |
| `blackbox_flash.s` | Call the located SPI flash reader and stream the result; the reader address is patched into the `movhi`/`ori` pair at offsets `0x40`/`0x44`. |

## Why this exists

Newer KC02 firmware builds move the application code, so the fixed addresses in
the original RAM-shim recipe no longer apply. Every AX3292 KC02, however, keeps
the USB endpoint buffers at fixed low-SRAM addresses (`0x8040` TX, `0x8250`
RX). A payload can therefore be staged into the RX buffer with an ordinary SCSI
`WRITE(10)` command and executed through the vendor command's host-supplied
function pointer:

1. `WRITE(10)` LBA 1 (the unused gap before the first partition) copies the
   512-byte staging blob — CBW-clobber-safe payload at offset `0x40` — into
   `0x8250`.
2. Vendor `0xCD` with `CDB[1..4] = 0x8290` (`0x8250 + 0x40`) calls the staged
   payload with `r3 = &cmd_ctx`, `r4 = &xport_ctx`.
3. The payload reads `cmd_ctx+0x24` (length), `cmd_ctx+0x38` (address), and
   `xport_ctx+0x24` (the firmware's own bulk-TX pointer), copies through
   `0x8040`, and streams data back.

The staging sector is saved and restored after the session.

## Rebuilding

```sh
or1k-elf-as  -o blackbox_read.o  blackbox_read.s
or1k-elf-objcopy -O binary blackbox_read.o blackbox_read.bin
```

Then refresh `BLACKBOX_READ_PAYLOAD` / `BLACKBOX_FLASH_PAYLOAD` in
`tools/dump_firmware.py` from the resulting `.bin` files.
