#!/usr/bin/env python3
"""Offline original-KC02 factory-layout review. Never writes a camera or SD card.

Only the pinned original dump is an admissible template. A layout candidate is
not flash-qualified. Install/update filenames are deliberately refused.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

STOCK_SHA256 = '218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5'
FLASH_SIZE = 0x400000
APP_START = 0x2600
APP_BYTES = 0x644 * 512
ENTRY = 0x0200046c
LOAD = 0x02000000
INSTALL_NAMES = {'destbin.bin', 'destbin.img', 'firmware.img'}


def inspect(data):
    if len(data) != FLASH_SIZE:
        raise ValueError('original KC02 candidate must be exactly 4 MiB')
    if data[4:8] != b'BLDR' or data[510:512] != b'\x55\xaa' or sum(data[:512]) & 255:
        raise ValueError('invalid BLDR header/signature/checksum')
    if data[0x304:0x308] != b'SFAT':
        raise ValueError('missing SFAT marker')
    fields = struct.unpack_from('<5I', data, 0x20)
    if fields != (LOAD, 0x13, 0x644, 0, 0x01234567) or struct.unpack_from('<I', data, 0x34)[0] != 0x01234567:
        raise ValueError('not the pinned original KC02 boot layout')
    return {'load': LOAD, 'entry': ENTRY, 'app_offset': APP_START,
            'app_bytes': APP_BYTES, 'factory_copy_end': APP_START + APP_BYTES}


def candidate(stock, ram, manifest):
    if hashlib.sha256(stock).hexdigest() != STOCK_SHA256:
        raise ValueError('template is not the pinned original KC02 dump; no other board is supported')
    layout = inspect(stock)
    if manifest.get('board') != 'original' or manifest.get('kind') != 'UNQUALIFIED BRINGUP RAM':
        raise ValueError('only the original-board bring-up artifact is accepted, not the Luma RAM app')
    if manifest.get('selftest') or manifest.get('load') != LOAD or manifest.get('entry') != ENTRY:
        raise ValueError('wrong boot entry or self-test artifact (self-test is QEMU-only)')
    if manifest.get('sha256') != hashlib.sha256(ram).hexdigest() or manifest.get('size') != len(ram):
        raise ValueError('RAM manifest hash/size mismatch')
    if len(ram) <= ENTRY - LOAD or len(ram) > APP_BYTES:
        raise ValueError('RAM artifact is empty or exceeds the actual factory copy span')
    out = bytearray(stock)
    out[APP_START:APP_START+APP_BYTES] = ram + b'\xff' * (APP_BYTES-len(ram))
    if out[:APP_START] != stock[:APP_START] or out[APP_START+APP_BYTES:] != stock[APP_START+APP_BYTES:]:
        raise AssertionError('candidate changed boot/header/resources outside its declared span')
    inspect(out)
    report = dict(layout, board='original', bytes=len(out),
                  sha256=hashlib.sha256(out).hexdigest(), template_sha256=STOCK_SHA256,
                  ram_sha256=manifest['sha256'], changed_span=[APP_START, APP_START+APP_BYTES],
                  flash_qualified=False, recovery_writer=False, hardware_tested=False,
                  warning='LAYOUT REVIEW ONLY. No physical recovery, SD recovery writer or cold-boot validation.')
    return bytes(out), report


def create(stock_path, ram_path, manifest_path, output):
    output = Path(output)
    if output.name.lower() in INSTALL_NAMES or output.with_suffix(output.suffix+'.json').name.lower() in INSTALL_NAMES:
        raise ValueError('refusing an auto-update filename; use DestBin.candidate.bin for offline review')
    report_path = output.with_suffix(output.suffix+'.json')
    if output.exists() or report_path.exists():
        raise FileExistsError('candidate/report output exists; choose fresh paths')
    data, report = candidate(Path(stock_path).read_bytes(), Path(ram_path).read_bytes(),
                             json.loads(Path(manifest_path).read_text()))
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation only; never overwrite a factory dump or an old report.
    with output.open('xb') as f:
        f.write(data)
    with report_path.open('x') as f:
        f.write(json.dumps(report, indent=2)+'\n')
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stock', type=Path, required=True)
    p.add_argument('--ram', type=Path, required=True)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    try:
        report = create(a.stock, a.ram, a.manifest, a.output)
    except (ValueError, OSError) as exc:
        p.error(str(exc))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
