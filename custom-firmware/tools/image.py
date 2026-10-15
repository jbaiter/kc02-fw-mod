#!/usr/bin/env python3
"""Pack a Luma RAM image into the LUMAIMG container. Offline only.

This is the missing piece between "Luma runs from RAM" and "Luma can be
installed": it produces a container the device's own validator accepts.

The layout is not re-derived here. It is written to match image_validate() in
custom-firmware/src/update.c field for field, and tests/test_image_pack.py
compiles that production validator and runs it against the output, so the two
cannot drift apart silently.

Three deliberate refusals, because the point of this tool is a safe first
install rather than a convenient one:

  * flash_qualified, recovery_writer and hardware_tested are always reported
    false. Nothing has performed an external SPI read and the recovery gate has
    never run on silicon, so a caller cannot mistake a well-formed container for
    a qualified one.
  * the install filenames are refused, so a review artifact cannot be dropped
    onto a card by accident.
  * a nonzero flags field is refused. image_validate() requires it to be zero, and
    a "development" flag is exactly what an updater must reject.

Nothing here writes a camera, a card, or NOR.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

MAGIC = b'LUMAIMG\x00'
HEADER = 64
SLOT_SIZE = 0x180000
LOAD = 0x02000000
ENTRY = 0x02000000
BOARD_BITS = {'original': 1, 'x8d': 2, 'z12': 4}
INSTALL_NAMES = {'luma.img', 'firmware.img', 'destbin.bin', 'destbin.img'}


def pack(payload, boards, version, flags=0):
    """Build the 64-byte header plus payload, matching image_validate()."""
    if not payload:
        raise ValueError('payload is empty')
    if len(payload) > SLOT_SIZE:
        raise ValueError(f'payload is {len(payload)} bytes, over the {SLOT_SIZE}-byte slot')
    if not boards or boards & ~7:
        raise ValueError('board mask must be a nonzero subset of original|x8d|z12')
    if not version:
        raise ValueError('release version must be nonzero')
    if flags:
        raise ValueError('flags must be zero; image_validate() rejects a nonzero flags field')

    header = bytearray(HEADER)
    header[0:8] = MAGIC
    header[8] = 1                        # format version low byte
    struct.pack_into('<H', header, 10, HEADER)
    struct.pack_into('<I', header, 12, boards)
    struct.pack_into('<I', header, 16, len(payload))
    struct.pack_into('<I', header, 20, LOAD)
    struct.pack_into('<I', header, 24, ENTRY)
    struct.pack_into('<I', header, 28, version)
    struct.pack_into('<I', header, 32, zlib.crc32(payload) & 0xffffffff)
    struct.pack_into('<I', header, 36, 0)          # header CRC is computed over this zeroed
    struct.pack_into('<I', header, 36, zlib.crc32(bytes(header)) & 0xffffffff)
    return bytes(header) + payload


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ram', type=Path, required=True)
    ap.add_argument('--board', choices=sorted(BOARD_BITS), default='original')
    ap.add_argument('--version', type=lambda v: int(v, 0), required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--manifest', type=Path,
                    help='optional build manifest; its hashes are recorded for review')
    a = ap.parse_args()

    report_path = a.output.with_suffix(a.output.suffix + '.json')
    if a.output.name.lower() in INSTALL_NAMES or report_path.name.lower() in INSTALL_NAMES:
        raise SystemExit('refusing an auto-update filename; use Luma.candidate.img')
    if a.output.exists() or report_path.exists():
        raise SystemExit('candidate/report output exists; choose fresh paths')

    payload = a.ram.read_bytes()
    boards = BOARD_BITS[a.board]
    image = pack(payload, boards, a.version)

    manifest = json.loads(a.manifest.read_text()) if a.manifest and a.manifest.is_file() else None
    if manifest is not None and manifest.get('kind', '').startswith('RAM RUN ONLY'):
        raise SystemExit('that artifact is the RAM-run bench image, not the shipping image')

    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_bytes(image)
    report = dict(
        board=a.board, boards=boards, version=a.version,
        bytes=len(image), payload_bytes=len(payload),
        image_sha256=hashlib.sha256(image).hexdigest(),
        payload_sha256=hashlib.sha256(payload).hexdigest(),
        payload_crc32='0x%08x' % (zlib.crc32(payload) & 0xffffffff),
        header_crc32='0x%08x' % struct.unpack_from('<I', image, 36)[0],
        load=hex(LOAD), entry=hex(ENTRY), slot_size=hex(SLOT_SIZE),
        build_manifest=manifest,
        flash_qualified=False,
        recovery_writer=False,
        hardware_tested=False,
        validated_by='custom-firmware/src/update.c image_validate()',
        warning=('CONTAINER ONLY. Not flash-qualified: no external SPI read has been '
                 'verified and the recovery gate has never run on this hardware. The safe '
                 'first install is the bringup image, not Luma.'))
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print(f'wrote {a.output} ({len(image):,} bytes; payload {len(payload):,})')
    print(f'  board mask {boards}, version {a.version}, header CRC '
          f'{report["header_crc32"]}, payload CRC {report["payload_crc32"]}')
    print('  flash_qualified=False recovery_writer=False hardware_tested=False')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
