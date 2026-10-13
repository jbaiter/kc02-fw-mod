#!/usr/bin/env python3
"""Build a custom firmware image with the KC02 UI routing patch applied.

Behaviour patched in:
  - boot enters the camera screen (mode 3) instead of the main menu
  - UP then POWER within 1 s  -> settings
  - DOWN then POWER within 1 s -> photo viewer
  - POWER inside either -> back to the camera screen

Everything stays in place: three stock POWER-handler bodies are replaced with code that fits their
existing 100-byte slots, two camera keymap entries are repointed, one settings keymap entry is
repointed, and one boot immediate changes 6 to 3. No byte of the image is added and no assumed-free
region is used, which is why the result should still be loadable: the application image keeps its
original extent.

Usage:
    patch_custom_ui.py -i firmware/original.bin -o patched/custom_ui/custom_ui.bin
    patch_custom_ui.py -i firmware/original.bin --dry-run
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MAP = 0x01FFDA00                     # CPU = file + MAP
SOURCE_SHA256 = '218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5'

# (cpu_address, expected_stock_word, new_word, description)
WORD_PATCHES = [
    (0x020004AC, 0x9C600006, 0x9C600003, 'boot: initial non-USB mode 6 -> 3 (camera)'),
    (0x020C2B6C, 0x02009890, 0x02016C14, 'camera keymap msg 0x1B (UP) -> camera_arm_up'),
    (0x020C2B74, 0x02009568, 0x02016C28, 'camera keymap msg 0x1C (DOWN) -> camera_arm_down'),
    (0x020C5470, 0x02016C14, 0x0200BD4C, 'settings keymap msg 0x24 (POWER) -> power_to_camera'),
]
# replaced handler bodies, taken from the build output
SLOT_FILES = [('slot_power', 0x02009F08, 'camera_power_router'),
              ('slot_settings', 0x02016C14, 'camera_arm_up / camera_arm_down'),
              ('slot_playback', 0x0200BD4C, 'power_to_camera')]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('-i', '--image', type=Path, default=Path('firmware/original.bin'))
    ap.add_argument('-o', '--out', type=Path)
    ap.add_argument('--build', type=Path, default=Path('/tmp/kc02-custom-ui-build'),
                    help='directory produced by build_ui_router.py')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()

    data = bytearray(a.image.read_bytes())
    digest = hashlib.sha256(data).hexdigest()
    if digest != SOURCE_SHA256:
        raise SystemExit(f'source image is not the pinned build:\n  expected {SOURCE_SHA256}\n  got      {digest}')
    if not a.build.exists():
        raise SystemExit(f'build directory {a.build} missing; run build_ui_router.py first')

    plan, originals = [], {}
    for cpu, expect, new, note in WORD_PATCHES:
        off = cpu - MAP
        have = struct.unpack_from('<I', data, off)[0]
        if have != expect:
            raise SystemExit(f'0x{cpu:08X} holds 0x{have:08X}, expected the stock 0x{expect:08X}')
        originals[off] = bytes(data[off:off + 4])
        plan.append((off, 4, struct.pack('<I', new), note))

    for name, cpu, label in SLOT_FILES:
        blob = (a.build / f'{name}.bin').read_bytes()
        off = cpu - MAP
        originals[off] = bytes(data[off:off + len(blob)])
        plan.append((off, len(blob), blob, f'{name} replaces the {label} body ({len(blob)} B)'))

    print(f'source  {a.image}  sha256 {digest[:16]}')
    print('plan:')
    for off, size, blob, note in plan:
        print(f'   0x{off:08X} +{size:<4d}  {note}')
    total = sum(size for _, size, _, _ in plan)
    print(f'   {len(plan)} spans, {total} bytes changed of {len(data)}')

    if a.dry_run:
        print('\ndry run: nothing written')
        return 0
    if a.out is None:
        raise SystemExit('--out is required unless --dry-run')

    for off, size, blob, _ in plan:
        data[off:off + size] = blob

    # verify: every changed byte must fall inside a planned span, and each span must have changed
    src = a.image.read_bytes()
    planned = sorted((off, off + size - 1) for off, size, _, _ in plan)
    changed = [i for i in range(len(data)) if data[i] != src[i]]
    outside = [i for i in changed if not any(lo <= i <= hi for lo, hi in planned)]
    if outside:
        raise SystemExit(f'{len(outside)} changed bytes fall outside the plan, first at 0x{outside[0]:X}')
    empty = [(lo, hi) for lo, hi in planned if not any(lo <= i <= hi for i in changed)]
    if empty:
        raise SystemExit(f'planned spans with no change: {[(hex(a), hex(b)) for a, b in empty]}')

    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_bytes(data)
    print(f'\nwrote {a.out}  sha256 {hashlib.sha256(data).hexdigest()}')
    print(f'verified: {len(changed)} changed bytes, all inside the {len(planned)} planned spans')

    (a.out.parent / 'custom_ui_patch.json').write_text(json.dumps(dict(
        source=str(a.image), source_sha256=digest, output=str(a.out),
        output_sha256=hashlib.sha256(data).hexdigest(),
        spans=[dict(file_offset=f'0x{o:08X}', cpu=f'0x{o + MAP:08X}', size=s, note=n)
               for o, s, _, n in plan],
        originals={f'0x{o:08X}': b.hex() for o, b in originals.items()},
    ), indent=2) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
