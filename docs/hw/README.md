# Hardware scripts

Hardware runners use the stock USB vendor command (`0xCD`) and pinned routines
from `tools/dump_firmware.py`. Serialize runs with
`flock -n "$HOME/kc02-hw/usb.lock" <command>`. Before RAM execution, the runner
checks firmware signatures, executes a return-only canary and verifies uploads
by readback. Keep camera SD volumes unmounted.

Current bench procedure, measured contracts and emulator changes:
[2026-10-15.md](2026-10-15.md). Save evidence under `~/kc02-hw/`, not `/tmp`.

| Script | Purpose | Writes? |
|---|---|---|
| `observe_state.py` | Read-only state snapshot over declared regions | no |
| `scan_ram_access.py` | Offline: derive global addresses/widths from a listing (not a USB script) | no |
| `execute_payload.py` | Upload and run a payload; save code, inputs, raw reply and JSON to a new persistent directory | RAM; sensor/ADC/TT probes also change peripherals reversibly |
| `audit_image.py`, `audit_bss.py` | Offline image and `.bss` audits | no |
| `mode_walk.py`, `usb_diag.py` | Mode walking and USB diagnostics | no |
| `make_print_test.py`, `make_carrier.py` | Generate the printed test pattern and JPEG carriers | no |
| `payloads/` | Probe sources and historical `.bin` files; rebuild current probes with `build_probes.py` | see each source's safety comment |
| `post_repair/` | Post-repair print results, runners and payloads; see its README | RAM only |
| `show_luma.py` | Run Luma from RAM on the camera: allocate a block, link for it, upload, run, capture the frame. `--takeover` jumps to Luma's real entry and never returns | RAM; the display and camera power registers it changes are restored, or the takeover leaves Luma running until a pinhole reset |
| `run_ram_image.py` | RAM-run loader: verify, upload with readback, run, capture, restore and verify. `--allocated` skips snapshot/restore for a block the firmware already handed out | RAM in the declared window only |
| `payloads/lumarun.c`, `payloads/ramalloc.c` | Bounded Luma entry runner and allocator reservation | see each source |
| `build_probes.py` | Offline LE/no-delay build with ELF/SPR policy checks | host files only |

**Region lengths in `observe_state.py` are firmware evidence, not assumptions.** Use
`scan_ram_access.py` to check them, and never pad a region to cover a neighbouring global. See the
region-length section of [../HARDWARE_OBSERVATION.md](../HARDWARE_OBSERVATION.md).

Full print results and the payload documentation are in
[post_repair/README.md](post_repair/README.md).
