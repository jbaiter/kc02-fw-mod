# KC02 hardware night — the complete playbook

Companion to `UI_STATE_MACHINE.md` / `PRINT_PIPELINE.md`. Everything below is
RAM-only: **any hang = power-cycle, nothing can brick** (flash writes are
guarded and unused tonight). Firmware backup md5 `7236c4c1…` must stay untouched.

## 0. Ground rules
- Use only `tools/kc02_usb.py` commands; never drive the `DestBin.bin` SD
  updater tonight.
- Power-cycle is always a safe reset (all pokes vanish).
- Keep the camera powered (fresh batteries / USB power) — auto-power-off
  mid-test just means redoing a step.

## 1. Environment prep (camera unplugged)
```bash
pip install pyusb                      # + libusb (apt: libusb-1.0-0)
sudo tee /etc/udev/rules.d/99-kc02.rules <<'EOF'
SUBSYSTEM=="usb", ATTR{idVendor}=="0219", MODE="0666", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="1908", MODE="0666", TAG+="uaccess"
EOF
sudo udevadm control --reload && sudo udevadm trigger

cd ~/.src/kc02-fw-mod
python3 tools/kc02_usb.py selftest      # offline guard check, expect OK
```
Disable SD automount tonight (the tool re-attaches `usb-storage` on exit; if
Linux mounts the card while the camera saves photos, both may write):
`pkill -f gvfs-udisks2-volume-monitor` or turn off automount in udisks.

## 2. Identify the mode (plug in, 1 min)
```bash
lsusb | grep -Ei '0219|1908'
```
| See | Meaning |
|---|---|
| `0219:3280` | mode 0, ideal — proceed |
| `1908:3283` | mode 2 composite, works — proceed |
| `1908:3282` | webcam-only mode 1: no bulk EPs, dead end — unplug/replug |

## 3. Phase A — the flash-window question
```bash
python3 tools/kc02_usb.py probe
```
- **`BLDR` present** → flash is CPU-mapped at `0x01FFDA00`; dump via route A.
- **Absent** → SPI-only confirmed; dump via route B.
- If the camera stops answering: power-cycle (known bus-error risk of this one probe).

## 4. Phase B — RAM + execution characterization
```bash
python3 tools/kc02_usb.py dump-ram --out ram1.bin     # baseline RAM capture
python3 tools/kc02_usb.py canary                      # l.jr r9 stub test
```
- **Canary returns normally** → RAM is executable and I-cache is coherent →
  the SPI shim and RAM code in general are viable.
- **Canary hangs** → power-cycle; RAM code execution is not viable; the whole
  RAM-code approach (shim, stubs) is dead and we go persistent-only later.

## 5. Phase C — THE 4 MB FLASH DUMP (the original goal)
Route A (probe said BLDR):
```bash
python3 tools/kc02_usb.py read --addr 0x01FFDA00 --len 0x400000 --out flash.bin
```
Route B (SPI-only):
```bash
python3 tools/kc02_usb.py load-shim      # once per power-cycle
python3 tools/kc02_usb.py flash-read --offset 0 --len 0x400000 --out flash.bin
```
Verify:
```bash
md5sum flash.bin        # expect 7236c4c1bd3f12b1d85d33a9bebdf510
```
- **md5 matches the backup** → end-to-end validation of everything: mapping,
  USB primitives, shim, the lot. Live flash == backup.
- **differs** → expected possibility (this camera's history includes Flash
  #1..#19 experiments). Save it as `flash-live.bin`; diff regions vs the
  backup (`cmp -l | head`). Either result is valuable data.
- Quick sanity: bytes 4..8 = `BLDR`; file tail `0x307200..` all `FF` on a stock image.

## 6. Phase D — camera usable while USB-tethered
```bash
python3 tools/kc02_usb.py unlock-usb --dry-run   # read the plan
python3 tools/kc02_usb.py unlock-usb             # kill the SD-lock screen
python3 tools/kc02_usb.py usb-hoist              # BOT service every frame
```
Then poke around the camera UI (nav keys, playback) and after each action run e.g.
`python3 tools/kc02_usb.py read --addr 0x020D416E --len 1` — if the tool still
answers, the hoist kept USB alive through navigation. If the SD screen ever
reappears, re-run `unlock-usb`. (`unlock-usb --restore` reverts everything.)

## 7. Phase E — child-friendly prototype (live only)
```bash
python3 tools/kc02_usb.py child-lock --dry-run  # inspect the 3 words + 2 stubs
python3 tools/kc02_usb.py child-lock           # apply + live jump to camera
```
Verify: camera jumps to viewfinder; **POWER alone does nothing**; **tap ↑ then
POWER opens the menu**; `child-lock adult-on` makes POWER work directly,
`adult-off` restores the sequence. Optionally take + print a shot while
tethered (instant-print fun — mind the SD automount note).
- **Honest limitation:** the boot-word poke vanishes at power-cycle, so tonight
  you verify the *live* gate/jump only. True boot-to-camera needs the
  persistent recipe (`kc02_unlock.py plan-persistent`) — NOT tonight.
- Bonus validation: this proves whether code-word pokes take effect (hoist +
  gate words live in code RAM). Record what you observe.
- Done: `child-lock --restore`.

## 8. Wrap-up
Save everything to `~/kc02-hardware-$(date +%F)/`: `probe.txt`, `flash.bin` +
md5, `ram1.bin`, one-paragraph observations per phase (canary behavior, hoist
alive?, gate sequence?, any hang). Report back and I'll fold results into the
docs and close the residual-risk list.

Next-session decisions after tonight: persistent flash plan (canary string
change first), LUT art-style experiments (needs a poke-guard extension to reach
`0x0207C640`), tracing `print_page_submit 0x02004588`.

## Failure quick-reference
| Symptom | Likely cause | Action |
|---|---|---|
| tool: permission denied | udev rule missing | reload rules, replug |
| tool: busy | other process/driver holds iface 4 | close managers, replug |
| probe hangs camera | unmapped read bus-error | power-cycle |
| canary hangs | RAM not executable / I-cache | power-cycle, note result |
| UI freezes after poke | wrong stub behavior | power-cycle (safe) |
