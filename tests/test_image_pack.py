"""Verify the LUMAIMG packer against the device's own validator.

custom-firmware/src/update.c owns the container contract. Rather than restate the
layout in Python and hope, this compiles that production validator and runs it
over the packer's output, then checks that every field the validator inspects is
actually load-bearing by tampering with it one field at a time.

No camera, card or NOR is touched.
"""
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'custom-firmware'
LUMA_BUILD = CORE / 'build/original'

spec = importlib.util.spec_from_file_location('luma_image', CORE / 'tools/image.py')
image_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(image_tool)

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "update.h"

static u8 *g;
static u32 gsize;

static int rd(void *ctx, u32 off, u8 *out, unsigned n)
{
    (void)ctx;
    if ((size_t)off + n > (size_t)gsize) { return 0; }
    memcpy(out, g + off, n);
    return 1;
}

int main(int argc, char **argv)
{
    FILE *f;
    ImageSource src;
    ImageInfo info;
    int ok;
    if (argc != 3) { return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { return 2; }
    fseek(f, 0, SEEK_END); gsize = (u32)ftell(f); fseek(f, 0, SEEK_SET);
    g = malloc(gsize);
    if (!g || fread(g, 1, gsize, f) != gsize) { return 2; }
    fclose(f);
    src.context = NULL; src.read = rd; src.size = gsize;
    memset(&info, 0, sizeof(info));
    ok = image_validate(&src, (u32)strtoul(argv[2], NULL, 0), &info);
    printf("%d %u %u %u %u %u\n", ok, info.boards, info.bytes, info.entry,
           info.version, info.crc);
    return ok ? 0 : 1;
}
'''

PAYLOAD = bytes(range(256)) * 8 + b'LUMA-PAYLOAD-TEST' * 7


@pytest.fixture(scope='module')
def validator(tmp_path_factory):
    cc = shutil.which('cc')
    if not cc:
        pytest.skip('C compiler unavailable')
    folder = tmp_path_factory.mktemp('validator')
    driver = folder / 'driver.c'
    driver.write_text(DRIVER)
    binary = folder / 'validator'
    subprocess.run([cc, '-std=gnu99', '-O1', '-I', str(CORE / 'include'),
                    str(driver), str(CORE / 'src/update.c'), str(CORE / 'src/crc.c'),
                    '-o', str(binary)], check=True)
    return binary


def run_validator(validator, image, board_bit=1, tmp_path=None):
    path = (tmp_path or Path('/tmp')) / 'candidate.img'
    path.write_bytes(image)
    result = subprocess.run([str(validator), str(path), str(board_bit)],
                            capture_output=True, text=True)
    fields = result.stdout.split()
    return (fields[0] == '1' if fields else False), fields


def test_packer_output_is_accepted_by_the_device_validator(validator, tmp_path):
    image = image_tool.pack(PAYLOAD, boards=1, version=7)
    ok, fields = run_validator(validator, image, 1, tmp_path)
    assert ok, f'validator rejected the packer output: {fields}'
    assert int(fields[1]) == 1          # boards
    assert int(fields[2]) == len(PAYLOAD)
    assert int(fields[3]) == 0x02000000  # entry
    assert int(fields[4]) == 7          # version
    assert int(fields[5]) == zlib.crc32(PAYLOAD) & 0xffffffff


def test_validator_rejects_each_tampered_field(validator, tmp_path):
    good = image_tool.pack(PAYLOAD, boards=1, version=7)

    def tamper(offset, value):
        b = bytearray(good)
        if isinstance(value, int):
            b[offset] = value
        else:
            b[offset:offset + len(value)] = value
        return bytes(b)

    cases = {
        'magic': tamper(0, b'XUMAIMG\x00'),
        'format version': tamper(8, 2),
        'header size': tamper(10, 63),
        'board mask zero': tamper(12, struct.pack('<I', 0)),
        'board mask extra bit': tamper(12, struct.pack('<I', 0x8)),
        'payload size': tamper(16, struct.pack('<I', len(PAYLOAD) + 4)),
        'load address': tamper(20, struct.pack('<I', 0x02001000)),
        'entry address': tamper(24, struct.pack('<I', 0x02000004)),
        'version zero': tamper(28, struct.pack('<I', 0)),
        'payload crc': tamper(32, struct.pack('<I', 0xdeadbeef)),
        'header crc': tamper(36, struct.pack('<I', 0xdeadbeef)),
        'flags nonzero': tamper(40, 1),
        'reserved byte': tamper(44, 1),
        'payload byte': tamper(64 + 100, lambda: None) if False else tamper(64 + 100, (good[64 + 100] ^ 0xff)),
    }
    for name, candidate in cases.items():
        ok, fields = run_validator(validator, candidate, 1, tmp_path)
        assert not ok, f'validator accepted a tampered image ({name})'


def test_packer_refuses_unsafe_inputs():
    for kwargs in ({'boards': 0, 'version': 1}, {'boards': 8, 'version': 1},
                   {'boards': 1, 'version': 0}, {'boards': 1, 'version': 1, 'flags': 1}):
        with pytest.raises(ValueError):
            image_tool.pack(PAYLOAD, **kwargs)
    with pytest.raises(ValueError):
        image_tool.pack(b'', boards=1, version=1)
    with pytest.raises(ValueError):
        image_tool.pack(b'\x00' * (image_tool.SLOT_SIZE + 1), boards=1, version=1)


def test_shipping_image_packs_and_validates(validator, tmp_path):
    """The real Luma build must pack into a container the validator accepts."""
    raw = LUMA_BUILD / 'luma.ram.bin'
    manifest = LUMA_BUILD / 'manifest.json'
    if not raw.is_file() or not manifest.is_file():
        pytest.skip('Luma build not present in this checkout')
    record = json.loads(manifest.read_text())
    if record.get('kind', '').startswith('RAM RUN ONLY'):
        pytest.skip('bench image present; rebuild the shipping image')
    payload = raw.read_bytes()
    image = image_tool.pack(payload, boards=1, version=1)
    ok, fields = run_validator(validator, image, 1, tmp_path)
    assert ok, f'validator rejected the shipping Luma image: {fields}'
    assert int(fields[2]) == len(payload)
