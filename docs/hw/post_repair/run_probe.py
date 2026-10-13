#!/usr/bin/env python3
"""Run the diagnostic payload: printer/paper/battery readout, optionally plus a short test strip.

INPUT[0]=0 report only, 1 report + test print   INPUT[1]=feed lines
INPUT[2]=energy                                  INPUT[3]=threshold mode
"""
import argparse
import hashlib
import importlib.util
import json
import struct
import sys
import datetime
from pathlib import Path

REPO = Path('/home/jbaiter/.src/kc02-fw-mod')
HW = Path('/tmp/kc02-board-re/hw')
spec = importlib.util.spec_from_file_location('executor', str(HW / 'execute_payload.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
dump, cases = m.dump, m.cases
IMAGE = (REPO / 'firmware/original.bin').read_bytes()
assert hashlib.sha256(IMAGE).hexdigest() == '218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5'
BODIES = [(0x0203e5c4, 208, 'allocator body'), (0x0203e694, 244, 'free body'),
          (0x02059f84, 416, 'printer init body'), (0x0205a6c4, 440, 'print run A body'),
          (0x02059290, 276, 'battery classification body'), (0x020593a4, 40, 'sensor read body'),
          (0x020035b8, 32, 'battery ADC body'), (0x020034c4, 164, 'battery read body'),
          (0x020386b8, 36, 'gpio read body')]
dump.ROUTINE_SIGNATURES += tuple((a, IMAGE[a - 0x01ffda00:a - 0x01ffda00 + n], t) for a, n, t in BODIES)

STATUS = {0: 'ok', 8: 'no paper / cover', 9: 'low supply', 10: 'bad height',
          14: 'charging', 255: 'internal error'}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--print', dest='do_print', type=int, default=0)
    ap.add_argument('--lines', type=int, default=64)
    ap.add_argument('--energy', type=lambda v: int(v, 0), default=3)
    ap.add_argument('--threshold', type=lambda v: int(v, 0), default=1)
    ap.add_argument('--timeout-ms', type=int, default=180000)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    words = [a.do_print, a.lines, a.energy, a.threshold]
    code = (HW / 'payloads' / 'probe.bin').read_bytes()
    if len(code) > cases.CODE_LIMIT:
        raise SystemExit(f'payload is {len(code)} bytes, over the {cases.CODE_LIMIT}-byte window')
    packet = bytearray(cases.PAD)
    for i, word in enumerate(words):
        struct.pack_into('<I', packet, m.INPUT + 4 * i, word)
    packet = bytes(packet)
    report = dict(payload='probe', do_print=a.do_print, lines=a.lines, energy=a.energy,
                  threshold=a.threshold, code_bytes=len(code),
                  code_sha256=hashlib.sha256(code).hexdigest(),
                  started=datetime.datetime.now(datetime.timezone.utc).isoformat())
    usb, backend = dump.load_usb()
    cams, webcams = dump.find_cameras(usb, backend)
    dev = dump.select_camera(cams, webcams, 1)
    report['device'] = dump.device_label(dev)
    try:
        with m.PayloadTransport(usb, dev, a.timeout_ms) as t:
            t.check_firmware()
            print('  1. firmware signatures verified')
            t.blobs = {m.CANARY: dump.CANARY_BYTES, m.BASE: dump.CANARY_PATTERN}
            t.upload(m.CANARY, dump.CANARY_BYTES)
            t.upload(m.BASE, dump.CANARY_PATTERN)
            assert t._command(m.BASE, m.SIZE, m.ICACHE_INIT_FN) == dump.CANARY_PATTERN
            t.icache_ready = True
            assert t._command(m.BASE, m.SIZE, m.CANARY) == dump.CANARY_PATTERN
            t.canary_ok = True
            print('  2. canary ok')
            t.blobs = {m.CANARY: dump.CANARY_BYTES, m.SHIM: m.DISPATCH, m.CODE: code, m.BASE: packet}
            t.upload(m.SHIM, m.DISPATCH); t.upload(m.CODE, code); t.upload(m.BASE, packet)
            assert t._command(m.BASE, m.SIZE, m.ICACHE_INIT_FN) == packet
            t.icache_ready = True
            print('  3. uploaded and I-cache reset')
            parsed = m.parse_reply(t._command(m.BASE, m.SIZE, m.SHIM))
            out = parsed['output']
            report.update(magic_ok=parsed['magic_ok'], status='executed',
                          sensor_raw=out[0], battery_status=out[1], battery_adc=out[2],
                          battery_read=out[3], gpio_raw=out[4],
                          printer_counter=out[5], print_sensor=[out[6], out[7], out[8], out[9]],
                          app_level=out[10], app_threshold=out[11], app_density=out[12],
                          sensor_cache=out[13],
                          canvas=hex(out[14]) if a.do_print else None,
                          ink=out[16] if a.do_print else None,
                          canvas_hash=hex(out[17]) if a.do_print else None,
                          print_status=out[15] if a.do_print else None)
            print(f"  4. paper sensor (kc02_sensor_read) = {out[0]}  "
                  f"-> {'paper present' if out[0] == 0 else 'NO PAPER / LID OPEN'}")
            print(f"     gpioRead(0,0x80) = {out[4]}   cached byte @0x020E3220 = {out[13]}")
            print(f"     battery: status={out[1]} ({STATUS.get(out[1], 'unknown')}) "
                  f"adc={out[2]} read={out[3]} app level={out[10]}")
            print(f"     app: threshold={out[11]} density={out[12]}")
            print(f"     printer_counter(0x020D2EC8)={out[5]}  "
                  f"print_sensor(0x020D2EAC)={out[6]:02x} {out[7]:02x} {out[8]:02x} {out[9]:02x}")
            if a.do_print:
                print(f"  5. test strip printed: lines={a.lines} ink={out[16]} "
                      f"hash=0x{out[17]:08x} print_status={out[15]} "
                      f"({STATUS.get(out[15], 'unknown')})")
    except Exception as exc:
        report['status'] = 'fail'
        report['error'] = f'{type(exc).__name__}: {exc}'
        print('  FAILED: ' + report['error'])
    finally:
        a.out.parent.mkdir(parents=True, exist_ok=True)
        a.out.write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report.get('status') == 'executed' else 1


if __name__ == '__main__':
    sys.exit(main())
