#!/usr/bin/env python3
"""Drive the camera through UI modes with the mode payload, sampling RAM at each step.

For each mode: execute `ui_mode_request(mode, param)` via the payload, then read the
live state regions (dispatch table, mode table, app_context, sensor/printer/keypad).
Stops immediately on any transport failure so the device can be power-cycled.

Usage: mode_walk.py [--modes 6,3,2,4,5,7,8,9,10,1] [--param 0]
"""
import argparse, json, subprocess, sys, datetime
from pathlib import Path

HW = Path('/tmp/kc02-board-re/hw')
PY = Path('/home/jbaiter/.src/kc02-fw-mod/.env/bin/python')
REPO = Path('/home/jbaiter/.src/kc02-fw-mod')

def sh(cmd, timeout=300):
    p = subprocess.run([str(c) for c in cmd], cwd=str(REPO), capture_output=True, text=True, timeout=timeout)
    return p.returncode, (p.stdout or '') + (p.stderr or '')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--modes', default='6,3,2,4,5,7,8,9,10,1')
    ap.add_argument('--param', type=lambda v: int(v, 0), default=0)
    ap.add_argument('--read-addr', type=lambda v: int(v, 0), default=0x020D416C)
    a = ap.parse_args()
    out = HW / f'walk_{datetime.datetime.now().strftime("%Y%m%dT%H%M%S")}'
    out.mkdir(parents=True, exist_ok=True)
    log = []
    print(f"walking modes {a.modes} -> {out}")
    for m in [int(x, 0) for x in a.modes.split(',') if x.strip()]:
        rc, txt = sh([PY, HW / 'execute_payload.py', '--payload', 'mode', '--mode', hex(m),
                      '--param', hex(a.param), '--read-addr', hex(a.read_addr)])
        alive = sh(['lsusb', '-d', '0219:3280'])[1].strip() != ''
        step = dict(mode=m, exec_rc=rc, device_alive=alive, exec_output=txt[-600:])
        if rc == 0 and alive:
            rc2, txt2 = sh([PY, HW / 'observe_state.py'])
            snap = dict(step=m, at=str(datetime.datetime.now()), output=txt2[-800:],
                        dispatch=(HW / 'snapshot01' / 'handler_table.bin').read_bytes().hex(),
                        app_context=(HW / 'snapshot01' / 'app_context.bin').read_bytes().hex(),
                        sensor=(HW / 'snapshot01' / 'sensor_item.bin').read_bytes().hex())
            (out / f'step_{m:02d}.json').write_text(json.dumps(snap, indent=2) + '\n')
            step['snapshot'] = str(out / f'step_{m:02d}.json')
            print(f"  mode {m:2d}: executed, device alive, state sampled")
        else:
            print(f"  mode {m:2d}: STOP (rc={rc}, device_alive={alive})")
            log.append(step); break
        log.append(step)
    (out / 'walk.json').write_text(json.dumps(log, indent=2) + '\n')
    return 0

if __name__ == '__main__':
    sys.exit(main())
