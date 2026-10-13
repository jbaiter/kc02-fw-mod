#!/usr/bin/env python3
"""Guarded execution of the artprint payload (maze / resolution card).

Same envelope as execute_payload.py: firmware body guards, canary, verified uploads,
I-cache reset. Passes six input words and reports the canvas ink count and FNV hash
so a build-only run can be checked before any paper is used.
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
          (0x02059f84, 416, 'printer init body'), (0x0205a6c4, 440, 'print run A body')]
dump.ROUTINE_SIGNATURES += tuple((a, IMAGE[a - 0x01ffda00:a - 0x01ffda00 + n], t) for a, n, t in BODIES)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pattern', type=int, required=True, choices=[10, 11, 12, 13])
    ap.add_argument('--lines', type=int, required=True, help='feed lines (maze 1624, card 660, checker 504, tone 440)')
    ap.add_argument('--energy', type=lambda v: int(v, 0), default=3)
    ap.add_argument('--threshold', type=lambda v: int(v, 0), default=0)
    ap.add_argument('--seed', type=lambda v: int(v, 0), default=3292)
    ap.add_argument('--go', type=int, default=0)
    ap.add_argument('--timeout-ms', type=int, default=600000)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    words = [a.lines, a.energy, a.threshold, a.go, 0, a.pattern, a.seed]
    code = (HW / 'payloads' / 'artprint.bin').read_bytes()
    if len(code) > cases.CODE_LIMIT:
        raise SystemExit(f'payload is {len(code)} bytes, over the {cases.CODE_LIMIT}-byte window')
    packet = bytearray(cases.PAD)
    for i, word in enumerate(words):
        struct.pack_into('<I', packet, m.INPUT + 4 * i, word)
    packet = bytes(packet)
    report = dict(payload='artprint', pattern=a.pattern, feed_lines=a.lines, energy=a.energy,
                  threshold=a.threshold, seed=a.seed, go=a.go, code_bytes=len(code),
                  code_sha256=hashlib.sha256(code).hexdigest(),
                  packet_sha256=hashlib.sha256(packet).hexdigest(),
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
            print(f'  3. uploaded code {len(code)} B, packet {len(packet)} B')
            assert t._command(m.BASE, m.SIZE, m.ICACHE_INIT_FN) == packet
            t.icache_ready = True
            print('  4. I-cache reset ok')
            parsed = m.parse_reply(t._command(m.BASE, m.SIZE, m.SHIM))
            out = parsed['output']
            signed = lambda v: v - (1 << 32) if v >> 31 else v
            report.update(magic_ok=parsed['magic_ok'], canvas=hex(out[0]), feed_lines_echo=out[1],
                          ink_pixels=out[3], canvas_hash=hex(out[4]), density=out[5], battery=out[6],
                          print_status=out[7], status='executed')
            print(f"  5. executed: canvas=0x{out[0]:08x} lines={out[1]} ink={out[3]} "
                  f"hash=0x{out[4]:08x} print_status={out[7]} (0 = ok)")
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
