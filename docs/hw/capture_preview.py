#!/usr/bin/env python3
"""Read-only Y/UV capture from measured CSI/DE register reports.

Requires live stock camera mode. Does not pause DMA: these are diagnostic,
possibly torn frames, not atomic captures. Uses the existing observer's pinned
read-only transport, with ranges derived from the supplied DMA registers.
No RAM upload, callback execution, register write, SD or flash command.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import observe_state as observer

dump = observer.dump


def layout(report, csi):
    data = json.loads(report.read_text())
    if data.get('status') != 'executed' or data['payload'] != 'sprprobe':
        raise ValueError('requires a successful sprprobe report')
    r = {int(k, 0): v for k, v in data['reply']['registers'].items()}
    if csi:
        size = r[0xb180]
        width, height = size & 0xffff, size >> 16
        y, uv, stride = r[0xb060], r[0xb064], r[0xb068]
    else:
        size = r[0xc820]
        width, height = (size & 0xffff) + 1, (size >> 16) + 1
        # DE stores 24-bit SDRAM offsets; CSI stores complete CPU addresses.
        y, uv = r[0xc828] + 0x02000000, r[0xc830] + 0x02000000
        stride = r[0xc82c] * 4
        if r[0xc834] * 4 != stride:
            raise ValueError('this diagnostic requires equal Y/UV strides')
    if not (16 <= width <= 640 and 2 <= height <= 640 and
            not ((width | height | stride) & 1) and width <= stride <= 1024):
        raise ValueError('unsupported geometry/stride')
    lengths = (stride * height, stride * (height // 2))
    for addr, length in zip((y, uv), lengths):
        if addr & 3 or length & 3 or not 0x02103980 <= addr < addr + length <= 0x027fec00:
            raise ValueError('DMA range is outside the measured HAL pool')
    return dict(width=width, height=height, stride=stride, y=y, uv=uv,
                y_length=lengths[0], uv_length=lengths[1], source=str(report))


def ppm(y, uv, info):
    width, height, stride = (info[k] for k in ('width', 'height', 'stride'))
    rgb = bytearray()
    def clip(value):
        return max(0, min(255, value))
    for row in range(height):
        for col in range(width):
            lum = y[row * stride + col]
            off = row // 2 * stride + (col & ~1)
            u, v = uv[off] - 128, uv[off + 1] - 128
            rgb.extend((clip(lum + ((359*v) >> 8)),
                        clip(lum - ((88*u + 183*v) >> 8)),
                        clip(lum + ((454*u) >> 8))))
    return f'P6\n{width} {height}\n255\n'.encode() + rgb


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--csi-report', type=Path, required=True)
    ap.add_argument('--de-report', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True, help='new evidence directory')
    args = ap.parse_args()
    layouts = {'csi': layout(args.csi_report, True), 'de': layout(args.de_report, False)}
    args.output.mkdir(parents=True, exist_ok=False)
    regions = [('mode', 0x020ccd4c, 16), ('idle', 0x020ccdcc, 8)]
    for name, info in layouts.items():
        regions += [(name+'-'+plane, info[plane], info[plane+'_length']) for plane in ('y', 'uv')]
    # This script owns its observer module/context; no other caller shares this
    # declared-region table. Parent _command still forbids writes/callbacks.
    observer.REGIONS = regions
    report = dict(layouts=layouts, atomic=False)
    def save(name, data):
        (args.output / name).write_bytes(data)
        return hashlib.sha256(data).hexdigest()
    try:
        usb, backend = dump.load_usb()
        cams, webcams = dump.find_cameras(usb, backend)
        dev = dump.select_camera(cams, webcams, 1)
        report['device'] = dump.device_label(dev)
        with observer.ObserverTransport(usb, dev, 5000) as transport:
            dump.check_firmware(transport)
            def read(addr, length):
                return b''.join(transport._command(addr + off, min(512, length - off))
                                for off in range(0, length, 512))
            mode = read(0x020ccd4c, 16)
            save('mode-before.bin', mode)
            if struct.unpack('<4I', mode)[2] != 3:
                raise dump.DumpError('not in camera mode; refusing stale DMA capture')
            for name, info in layouts.items():
                hashes = []
                for sample in range(2):
                    y = read(info['y'], info['y_length'])
                    uv = read(info['uv'], info['uv_length'])
                    hashes.append(save(f'{name}-{sample}.y.bin', y))
                    save(f'{name}-{sample}.uv.bin', uv)
                    save(f'{name}-{sample}.ppm', ppm(y, uv, info))
                info['luma_hashes'] = hashes
                info['luma_changed'] = hashes[0] != hashes[1]
                print(f'{name}: {info["width"]}x{info["height"]}, pitch {info["stride"]}, '
                      f'luma_changed={info["luma_changed"]}')
            save('mode-after.bin', read(0x020ccd4c, 16))
            save('idle-after.bin', read(0x020ccdcc, 8))
        report['status'] = 'pass'
    except Exception as exc:
        report['status'] = 'fail'
        report['error'] = f'{type(exc).__name__}: {exc}'
        print(report['error'])
    finally:
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
