#!/usr/bin/env python3
"""Guarded execution of the fileprint payload, with a configurable USB timeout.

Same envelope as execute_payload.py: firmware body guards, canary, verified
read-back uploads, I-cache reset. The packet carries a NUL-terminated SD path at
offset 0, so the payload prints a JPEG that already exists on the card.
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
dump = m.dump
cases = m.cases
IMAGE = (REPO / 'firmware/original.bin').read_bytes()
assert hashlib.sha256(IMAGE).hexdigest() == '218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5'
BODIES = [(0x0203e5c4, 208, 'allocator body'), (0x02059f84, 416, 'printer init body'),
          (0x0205a6c4, 440, 'print run A body'), (0x02059290, 276, 'power classification body'),
          (0x0206c5b8, 200, 'VFS open body'), (0x0206c680, 120, 'VFS close body'),
          (0x0206caf8, 76, 'VFS fs_size body'), (0x0205adf0, 780, 'print-file body')]
dump.ROUTINE_SIGNATURES += tuple((a, IMAGE[a - 0x01ffda00:a - 0x01ffda00 + n], t) for a, n, t in BODIES)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--name', required=True, help='SD path, e.g. PHOTO/PHO00045.JPG')
    ap.add_argument('--energy', type=lambda v: int(v, 0), default=3)
    ap.add_argument('--layout', type=lambda v: int(v, 0), default=0)
    ap.add_argument('--p6', type=lambda v: int(v, 0), default=0)
    ap.add_argument('--go', type=int, default=0, help='1 prints; 0 only opens and sizes the file')
    ap.add_argument('--timeout-ms', type=int, default=180000)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()

    for key, value in (('name', a.name), ('energy', a.energy), ('layout', a.layout)):
        assert 0 <= value <= 255 if key != 'name' else len(value.encode()) < 60
    code = (HW / 'payloads' / 'fileprint.bin').read_bytes()
    if len(code) > cases.CODE_LIMIT:
        raise SystemExit(f'payload is {len(code)} bytes, over the {cases.CODE_LIMIT}-byte window')
    packet = bytearray(cases.PAD)
    packet[0:len(a.name) + 1] = a.name.encode() + b'\0'
    for i, word in enumerate((a.energy, a.layout, a.p6, a.go)):
        struct.pack_into('<I', packet, m.INPUT + 4 * i, word)
    packet = bytes(packet)

    report = dict(payload='fileprint', name=a.name, energy=a.energy, layout=a.layout, p6=a.p6, go=a.go,
                  code_bytes=len(code), code_sha256=hashlib.sha256(code).hexdigest(),
                  packet_sha256=hashlib.sha256(packet).hexdigest(),
                  started=datetime.datetime.now(datetime.timezone.utc).isoformat())
    usb, backend = dump.load_usb()
    cams, webcams = dump.find_cameras(usb, backend)
    dev = dump.select_camera(cams, webcams, 1)
    report['device'] = dump.device_label(dev)
    try:
        with m.PayloadTransport(usb, dev, a.timeout_ms) as t:
            t.check_firmware()
            print('  1. firmware signatures verified (incl. all print/VFS bodies)')
            t.blobs = {m.CANARY: dump.CANARY_BYTES, m.BASE: dump.CANARY_PATTERN}
            t.upload(m.CANARY, dump.CANARY_BYTES)
            t.upload(m.BASE, dump.CANARY_PATTERN)
            back = t._command(m.BASE, m.SIZE, m.ICACHE_INIT_FN)
            assert back == dump.CANARY_PATTERN, 'I-cache helper changed the canary buffer'
            t.icache_ready = True
            assert t._command(m.BASE, m.SIZE, m.CANARY) == dump.CANARY_PATTERN
            t.canary_ok = True
            print('  2. canary executed and returned its buffer unchanged')
            t.blobs = {m.CANARY: dump.CANARY_BYTES, m.SHIM: m.DISPATCH, m.CODE: code, m.BASE: packet}
            t.upload(m.SHIM, m.DISPATCH); t.upload(m.CODE, code); t.upload(m.BASE, packet)
            print(f'  3. uploaded + verified: code {len(code)} B, packet {len(packet)} B')
            back = t._command(m.BASE, m.SIZE, m.ICACHE_INIT_FN)
            assert back == packet, 'I-cache helper changed the packet'
            t.icache_ready = True
            print('  4. stock I-cache reset executed')
            parsed = m.parse_reply(t._command(m.BASE, m.SIZE, m.SHIM))
            report['reply'] = parsed
            out = parsed['output']
            fd, size, rc = (int(v) for v in (out[0], out[1], out[2]))
            signed_fd = fd - (1 << 32) if fd >> 31 else fd
            report.update(magic_ok=parsed['magic_ok'], fd=signed_fd, file_size=size,
                          print_status=rc if a.go else None, status='executed')
            print(f"  5. executed: fd={signed_fd} size={size} bytes print_status={rc if a.go else 'n/a'}"
                  f" app_battery={out[6]}")
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
