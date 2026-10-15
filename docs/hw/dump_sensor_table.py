#!/usr/bin/env python3
"""Dump the stock H63P init table out of RAM.

The table is data, so it cannot be decompiled. Instead this calls the stock
autodetect, which copies the selected sensor's table into a caller-supplied
buffer, then reads that buffer back.

That only works because of a fact found by decompiling the stock path: the sensor
is NOT on a hardware IIC controller. sensor_iic_start/stop/check_ack wrap
software_iic_start/stop/wait_ack, so the bus is GPIO bit-banged and this build
takes the SDK's USE_SOFTWARE_IIC0 path. The hardware IIC0 and IIC1 controllers
were therefore driving pins nothing was listening to, which is why every earlier
attempt failed and why stock's own hardware-IIC helper read 0xff. The payload
initialises the bit-banged bus before calling autodetect.

The target buffer is in the measured free span 0x020db200-0x020e30ff: all zero,
referenced by no static global, and unchanged across a full test batch.

No flash access of any kind.
"""
import argparse
import datetime
import json
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import execute_payload as hw
import dump_firmware as dump

SCRATCH = 0x020DC000
SCRATCH_LEN = 0x1000
FENCE = b'\xa5' * 4


class TableTransport(hw.PayloadTransport):
    """Allow reads inside the declared scratch span only."""

    def _approved_read(self, buffer, length):
        if SCRATCH <= buffer and buffer + length <= SCRATCH + SCRATCH_LEN and 0 < length <= hw.SIZE:
            return buffer
        return super()._approved_read(buffer, length)


def decode_table(data):
    """The stock loop walks pairs and stops at 0xFF 0xFF."""
    pairs = []
    for off in range(0, len(data) - 1, 2):
        reg, val = struct.unpack_from('<BB', data, off)
        if reg == 0xFF and val == 0xFF:
            return pairs, True
        pairs.append((reg, val))
    return pairs, False


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--payload-dir', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()

    code = (a.payload_dir / 'sensortable.bin').read_bytes()
    packet = hw.build_packet(words=[0])
    a.output.mkdir(parents=True, exist_ok=False)
    (a.output / 'sensortable.bin').write_bytes(code)
    report = dict(kind='stock init-table dump', scratch=hex(SCRATCH),
                  started=datetime.datetime.now(datetime.timezone.utc).isoformat())

    def save():
        (a.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')

    try:
        usb, backend = dump.load_usb()
        cams, webcams = dump.find_cameras(usb, backend)
        dev = dump.select_camera(cams, webcams, 1)
        report['device'] = dump.device_label(dev)
        with TableTransport(usb, dev, 5000) as t:
            t.check_firmware()
            print('  firmware signatures verified', flush=True)
            if t.hoist_active:
                raise dump.DumpError('a RAM hoist is active; power-cycle to stock first')

            reply = hw.execute_verified(t, code, packet)
            (a.output / 'reply.bin').write_bytes(reply)
            parsed = hw.parse_reply(reply)
            out = parsed['output']
            report['ops_pointer'] = '0x%08x' % out[0]
            report['ops_is_real_h63p'] = out[0] == 0x020E32A0
            print(f'  autodetect returned ops 0x{out[0]:08X} '
                  f'({"real H63P" if out[0] == 0x020E32A0 else "NOT the H63P entry"})', flush=True)

            data = t.read_region(SCRATCH, SCRATCH_LEN)
            (a.output / 'scratch.bin').write_bytes(data)
            report['scratch_sha256'] = __import__('hashlib').sha256(data).hexdigest()
            untouched = sum(1 for i in range(0, len(data), 4) if data[i:i+4] == FENCE)
            report['untouched_words'] = untouched
            print(f'  scratch: {untouched} words still hold the fence pattern', flush=True)

            pairs, term = decode_table(data)
            report['pairs'] = len(pairs)
            report['terminated'] = term
            report['table'] = [[r, v] for r, v in pairs]
            print(f'  decoded {len(pairs)} register/value pairs, terminator={term}', flush=True)
            for i, (r, v) in enumerate(pairs[:20]):
                print(f'    [{i:3d}] reg 0x{r:02x} = 0x{v:02x}', flush=True)
            if len(pairs) > 20:
                print(f'    ... {len(pairs) - 20} more', flush=True)
        report['status'] = 'pass'
    except Exception as exc:
        report['status'] = 'fail'
        report['error'] = f'{type(exc).__name__}: {exc}'
        print(f'  FAILED: {report["error"]}', flush=True)
    finally:
        report['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        save()
        print(f'  evidence: {a.output}', flush=True)
    return 0 if report.get('status') == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
