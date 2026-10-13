# Hardware scripts

Everything here talks to the camera over the stock USB vendor command (`0xCD`) using the pinned
routine in `tools/dump_firmware.py`. Runs are serialized with `flock /tmp/kc02-board-re/hw/usb.lock`
and, when they execute RAM code, they first verify the firmware body signatures, run a canary, and
read every upload back.

| Script | Purpose | Writes? |
|---|---|---|
| `observe_state.py` | Read-only state snapshot over declared regions | no |
| `scan_ram_access.py` | Offline: derive global addresses/widths from a listing (not a USB script) | no |
| `execute_payload.py` | Upload and run a RAM payload inside the verified free window | RAM only |
| `audit_image.py`, `audit_bss.py` | Offline image and `.bss` audits | no |
| `mode_walk.py`, `usb_diag.py` | Mode walking and USB diagnostics | no |
| `make_print_test.py`, `make_carrier.py` | Generate the printed test pattern and JPEG carriers | no |
| `payloads/` | Freestanding payload sources and built `.bin` files | RAM only |
| `post_repair/` | Post-repair print results, runners and payloads; see its README | RAM only |

**Region lengths in `observe_state.py` are firmware evidence, not assumptions.** Use
`scan_ram_access.py` to check them, and never pad a region to cover a neighbouring global. See the
region-length section of [../HARDWARE_OBSERVATION.md](../HARDWARE_OBSERVATION.md).

Full print results and the payload documentation are in
[post_repair/README.md](post_repair/README.md).
