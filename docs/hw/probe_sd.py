#!/usr/bin/env python3
"""Read-only SD routing/MBR/BPB probe using pinned stock SD read routines.

Run under the shared USB flock after a camera reset. Never creates a file or
writes a sector. Evidence goes to the explicitly supplied output directory.
"""
import argparse
import datetime
import json
from pathlib import Path

import show_luma as luma
from sd_safety import require_unmounted


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--payload-dir', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    report = dict(kind='read-only SD probe', started=datetime.datetime.now(datetime.timezone.utc).isoformat())
    try:
        code = (args.payload_dir / 'sdreadprobe.bin').read_bytes()
        (args.output / 'sdreadprobe.bin').write_bytes(code)
        usb, backend = luma.dump.load_usb()
        cams, webcams = luma.dump.find_cameras(usb, backend)
        dev = luma.dump.select_camera(cams, webcams, 1)
        require_unmounted(dev)
        with luma.LumaRamTransport(usb, dev, 10000, window=(0x02104000, 0x1000)) as t:
            t.check_firmware()
            if t.hoist_active:
                raise luma.dump.DumpError('active RAM hoist: reset to stock first')
            reply = luma.hw.execute_verified(t, code, luma.hw.build_packet(words=[0]))
            (args.output / 'reply.bin').write_bytes(reply)
            parsed = luma.hw.parse_reply(reply)
            report['reply'] = parsed
            out = parsed['output']
            if not parsed['magic_ok'] or out[25] or out[3] & 0x80000000 or out[6] & 0x80000000 or out[7] & 0x80000000:
                raise luma.dump.DumpError(f'SD probe refused/failed: {out}')
            (args.output / 'sector-0.bin').write_bytes(reply[:512])
            b = reply[512:1024]
            (args.output / 'boot-sector.bin').write_bytes(b)
            u16 = lambda n: int.from_bytes(b[n:n+2], 'little')
            u32 = lambda n: int.from_bytes(b[n:n+4], 'little')
            report['card'] = dict(sectors=out[2], partition_start=out[5],
                                  boot_signature=b[510:512].hex(), bytes_per_sector=u16(11),
                                  sectors_per_cluster=b[13], reserved=u16(14), fats=b[16],
                                  root_entries=u16(17), fat16_sectors=u16(22),
                                  fat32_sectors=u32(36), root_cluster=u32(44),
                                  total_sectors=u16(19) or u32(32))
            print(json.dumps(report['card'], indent=2), flush=True)
            report['status'] = 'pass'
        return 0
    except Exception as exc:
        report['status'] = 'fail'
        report['error'] = f'{type(exc).__name__}: {exc}'
        print(report['error'], flush=True)
        return 1
    finally:
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(f'evidence: {args.output}', flush=True)


if __name__ == '__main__':
    raise SystemExit(main())
