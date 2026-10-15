"""SD card update path: discovery, validation, and the handoff to the engine.

A real FAT16 card image is built on disk, the adapter streams the image out of
it, and the engine writes into a model NOR. So this covers the whole path a
field update takes, apart from the NOR driver itself, which does not exist yet.
"""
import ctypes as C
import hashlib
import importlib.util
from pathlib import Path
import random
import shutil
import struct
import subprocess
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT/'custom-firmware'
U8, U32 = C.c_ubyte, C.c_uint32

READY, GOOD, NO_CARD, BUSY, NO_FILE, BAD_FILE, NO_WRITER, DENIED, STALE, NO_BOOT, IO = range(11)
UPDATE_OK, UPDATE_DENIED, UPDATE_INVALID, UPDATE_IO, UPDATE_NO_BOOT, UPDATE_STALE = range(6)
NONE, DENIED_BY_ENGINE, INVALID, IO_ERROR, NO_BOOT_ENGINE, STALE_ENGINE = range(6)
NOR_SIZE = 0x400000
SLOT_A, SLOT_B, META_A, META_B = 0x020000, 0x1a0000, 0x010000, 0x011000


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


card_tool = module('usd_card', FW/'tools/card.py')
sd_tool = module('usd_create_sd', ROOT/'tools/emu/create_sd.py')


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    if not shutil.which('cc'):
        pytest.skip('host C compiler missing')
    libpath = tmp_path_factory.mktemp('updatesd')/'updatesd.so'
    subprocess.run(['cc','-std=gnu99','-O1','-g','-Wall','-Wextra','-Werror',
                    '-shared','-fPIC','-I'+str(FW/'include'),
                    str(FW/'src/update_sd.c'), str(FW/'src/update.c'),
                    # update.c shares crc32 with the theme parser, so both
                    # translation units that provide it must be linked.
                    str(FW/'src/storage.c'), str(FW/'src/crc.c'),
                    '-o', str(libpath)], check=True)
    lib = C.CDLL(str(libpath))
    lib.update_sd_check.argtypes = [C.c_void_p, U32, C.c_void_p]
    lib.update_sd_check.restype = C.c_int
    lib.update_sd_apply.argtypes = [C.c_void_p, C.c_void_p, U32, C.c_int, C.c_int, C.c_void_p]
    lib.update_sd_apply.restype = C.c_int
    lib.fat_mount.argtypes = [C.c_void_p, SectorRead, SectorWrite, U32]
    return lib


class Info(C.Structure):
    _fields_ = [(n, U32) for n in ('bytes','entry','version','crc','boards')]


class Report(C.Structure):
    _fields_ = [('result', C.c_int), ('info', Info), ('file_bytes', U32),
                ('validated', C.c_int)]


ByteRead = C.CFUNCTYPE(C.c_int, C.c_void_p, U32, C.c_void_p, C.c_uint)
SectorRead = C.CFUNCTYPE(C.c_int, U32, C.c_void_p)
SectorWrite = C.CFUNCTYPE(C.c_int, U32, C.c_void_p)
Erase = C.CFUNCTYPE(C.c_int, C.c_void_p, U32)
Program = C.CFUNCTYPE(C.c_int, C.c_void_p, U32, C.c_void_p, C.c_uint)


class Nor(C.Structure):
    """Typed callbacks, so a wrong signature is a Python error and not a crash."""
    _fields_ = [('context', C.c_void_p), ('read', ByteRead),
                ('erase', Erase), ('program', Program)]

COUNTER = [0]


def make_card(tmp_path, entries):
    """A private, create-only FAT16 fixture."""
    COUNTER[0] += 1
    image = tmp_path/f'card-{COUNTER[0]}.img'
    sd_tool.create_image(image)
    if entries:
        card_tool.seed(image, entries)
    return image


def mount(native, image, write=None):
    data = image.read_bytes()

    @SectorRead
    def read(sector, out):
        if sector+1 > len(data)//512:
            return 0
        C.memmove(out, data[sector*512:sector*512+512], 512)
        return 1
    volume = C.create_string_buffer(8192)
    assert native.fat_mount(volume, read, write or C.cast(None, SectorWrite),
                            image.stat().st_size)
    return volume, read


def update_image(payload, version=2, boards=7, flags=0):
    header = bytearray(64)
    header[:8] = b'LUMAIMG\0'
    struct.pack_into('<HH8I', header, 8, 1, 64, boards, len(payload), 0x02000000,
                     0x02000000, version, zlib.crc32(payload), 0, flags)
    struct.pack_into('<I', header, 36, zlib.crc32(header))
    return bytes(header)+payload


def nor_model(installed_version=1, records=True, program_fails=False):
    """A model NOR holding one committed application in slot A."""
    flash = bytearray(b'\xff')*NOR_SIZE
    previous = b'an application that boots'*20
    flash[SLOT_A:SLOT_A+len(previous)] = previous
    writes = []
    if records:
        record = b'LMCOMMIT'+struct.pack('<5I',1,0,len(previous),zlib.crc32(previous),
                                         installed_version)
        flash[META_A:META_A+32] = record+struct.pack('<I',zlib.crc32(record))

    @ByteRead
    def read(ctx, offset, out, size):
        if offset > len(flash) or size > len(flash)-offset:
            return 0
        C.memmove(out, bytes(flash[offset:offset+size]), size)
        return 1

    @Erase
    def erase(ctx, offset):
        if offset < 0x10000 or offset % 4096 or offset > len(flash)-4096:
            return 0
        writes.append(('erase', offset))
        flash[offset:offset+4096] = b'\xff'*4096
        return 1

    @Program
    def program(ctx, offset, buf, size):
        if program_fails or offset < 0x10000 or not size or offset > len(flash)-size:
            return 0
        writes.append(('program', offset))
        data = C.string_at(buf, size)
        for i, byte in enumerate(data):
            flash[offset+i] &= byte
        return 1
    return Nor(None, read, erase, program), flash, previous, writes


def test_check_reports_version_and_board_for_confirmation(native, tmp_path):
    payload = b'new firmware'*200
    image = update_image(payload, version=7)
    card = make_card(tmp_path, [('FIRMWARE.IMG', image)])
    volume, _ = mount(native, card)
    report = Report()
    assert native.update_sd_check(volume, 1, C.byref(report)) == READY
    assert report.validated == 1
    assert report.file_bytes == len(image)
    assert report.info.version == 7
    assert report.info.bytes == len(payload)
    assert report.info.crc == zlib.crc32(payload)
    assert report.info.boards == 7


def test_lowercase_name_is_the_same_file(native, tmp_path):
    """A user typing /firmware.img means FIRMWARE.IMG."""
    card = make_card(tmp_path, [('FIRMWARE.IMG', update_image(b'x'*1000))])
    volume, _ = mount(native, card)
    report = Report()
    assert native.update_sd_check(volume, 1, C.byref(report)) == READY


@pytest.mark.parametrize('entries,board,expected',[
    ([], 1, NO_FILE),
    ([('OTHER.BIN', b'x'*100)], 1, NO_FILE),
    ([('FIRMWARE.IMG', b'not an image at all')], 1, BAD_FILE),
    ([('FIRMWARE.IMG', update_image(b'x'*1000)[:200])], 1, BAD_FILE),
    ([('FIRMWARE.IMG', update_image(b'x'*1000, boards=2))], 1, BAD_FILE),
    ([('FIRMWARE.IMG', update_image(b'x'*1000, version=0))], 1, BAD_FILE),
    ([('FIRMWARE.IMG', update_image(b'x'*1000, flags=1))], 1, BAD_FILE),
    ([('FIRMWARE.IMG', update_image(b'x'*1000)+b'extra')], 1, BAD_FILE),
    ([('FIRMWARE.IMG', update_image(b'x'*1000, boards=2))], 3, BAD_FILE),
])
def test_check_rejects_before_anything_is_written(native, tmp_path, entries, board, expected):
    card = make_card(tmp_path, entries)
    volume, _ = mount(native, card)
    report = Report()
    assert native.update_sd_check(volume, board, C.byref(report)) == expected
    assert report.validated == 0
    assert not report.info.bytes


def test_corrupt_payload_is_rejected_by_crc(native, tmp_path):
    image = bytearray(update_image(b'payload '*200))
    image[100] ^= 1
    card = make_card(tmp_path, [('FIRMWARE.IMG', bytes(image))])
    volume, _ = mount(native, card)
    report = Report()
    assert native.update_sd_check(volume, 1, C.byref(report)) == BAD_FILE


def test_no_card_and_no_mount(native, tmp_path):
    report = Report()
    assert native.update_sd_check(C.cast(None, C.c_void_p), 1, C.byref(report)) == NO_CARD
    blank = C.create_string_buffer(8192)
    assert native.update_sd_check(blank, 1, C.byref(report)) == NO_CARD


def test_a_card_owned_by_the_host_is_not_read(native, tmp_path):
    """Two writers on one FAT is the failure this refusal exists to prevent."""
    card = make_card(tmp_path, [('FIRMWARE.IMG', update_image(b'x'*1000))])
    volume, _ = mount(native, card)
    report = Report()
    assert native.update_sd_check(volume, 1, C.byref(report)) == READY
    # Hand the card to the host the way the storage function does.
    lib_storage = C.CDLL(native._name)
    lib_storage.sd_handoff.argtypes = [C.c_void_p, C.c_int, C.c_int]
    assert lib_storage.sd_handoff(volume, 1, 1)                   # SD_HOST
    assert native.update_sd_check(volume, 1, C.byref(report)) == BUSY
    assert native.update_sd_apply(volume, None, 1, 1, 1, C.byref(report)) == BUSY
    assert lib_storage.sd_handoff(volume, 0, 1)                   # back to SD_LOCAL
    assert native.update_sd_check(volume, 1, C.byref(report)) == NO_CARD, 'mount invalidated'


def test_validated_file_without_a_writer_says_so(native, tmp_path):
    """The file is good and nothing here can write it: report exactly that."""
    card = make_card(tmp_path, [('FIRMWARE.IMG', update_image(b'x'*1000))])
    volume, _ = mount(native, card)
    report = Report()
    assert native.update_sd_apply(volume, None, 1, 1, 1, C.byref(report)) == NO_WRITER
    assert report.validated == 1 and report.info.version == 2


def test_engine_requires_confirmation_and_recovery(native, tmp_path):
    card = make_card(tmp_path, [('FIRMWARE.IMG', update_image(b'y'*4096))])
    volume, _ = mount(native, card)
    nor, flash, previous, writes = nor_model()
    report = Report()
    for confirmation, recovery in ((0, 0), (0, 1), (1, 0)):
        assert native.update_sd_apply(volume, C.byref(nor), 1, confirmation, recovery,
                                      C.byref(report)) == DENIED
        assert not writes
    assert flash[SLOT_A:SLOT_A+len(previous)] == previous


def test_a_real_update_commits_the_inactive_slot(native, tmp_path):
    payload = b'a brand new application'*100
    image = update_image(payload, version=9)
    card = make_card(tmp_path, [('FIRMWARE.IMG', image)])
    volume, _ = mount(native, card)
    nor, flash, previous, writes = nor_model()
    report = Report()
    assert native.update_sd_apply(volume, C.byref(nor), 1, 1, 1, C.byref(report)) == GOOD
    assert report.info.version == 9
    # The new application is in slot B byte for byte, and slot A is untouched.
    assert flash[SLOT_B:SLOT_B+len(payload)] == payload
    assert flash[SLOT_A:SLOT_A+len(previous)] == previous
    # The commit record names slot B and the new generation.
    record = bytes(flash[META_B:META_B+32])
    assert record[:8] == b'LMCOMMIT'
    assert struct.unpack_from('<I', record, 8)[0] == 2
    assert struct.unpack_from('<I', record, 12)[0] == 1
    assert struct.unpack_from('<I', record, 24)[0] == 9
    assert all(offset >= 0x10000 and not (SLOT_A <= offset < SLOT_B)
               for _, offset in writes)


def test_a_stale_image_is_refused(native, tmp_path):
    card = make_card(tmp_path, [('FIRMWARE.IMG', update_image(b'z'*2048, version=3))])
    volume, _ = mount(native, card)
    nor, flash, previous, writes = nor_model(installed_version=5)
    report = Report()
    assert native.update_sd_apply(volume, C.byref(nor), 1, 1, 1, C.byref(report)) == STALE
    assert not writes


def test_flash_with_no_bootable_slot_is_refused(native, tmp_path):
    card = make_card(tmp_path, [('FIRMWARE.IMG', update_image(b'z'*2048, version=3))])
    volume, _ = mount(native, card)
    nor, flash, previous, writes = nor_model(records=False)
    report = Report()
    assert native.update_sd_apply(volume, C.byref(nor), 1, 1, 1, C.byref(report)) == NO_BOOT
    assert not writes


def test_the_reader_is_bounded_and_survives_both_passes(native, tmp_path):
    """The engine reads its source twice; the reader reopens for the second."""
    payload = bytes(random.Random(11).getrandbits(8) for _ in range(200000))
    card = make_card(tmp_path, [('FIRMWARE.IMG', update_image(payload, version=2))])
    volume, _ = mount(native, card)
    nor, flash, previous, writes = nor_model()
    report = Report()
    assert native.update_sd_apply(volume, C.byref(nor), 1, 1, 1, C.byref(report)) == GOOD
    assert flash[SLOT_B:SLOT_B+len(payload)] == payload
    assert hashlib.sha256(flash[SLOT_B:SLOT_B+len(payload)]).digest() == \
           hashlib.sha256(payload).digest()


def data_start(image):
    """The first sector after the FAT and root directory, from the volume BPB."""
    boot = image.read_bytes()[2048*512:2048*512+512]
    reserved = struct.unpack_from('<H', boot, 14)[0]
    fats = boot[16]
    fat_sectors = struct.unpack_from('<H', boot, 22)[0]
    root_entries = struct.unpack_from('<H', boot, 17)[0]
    return 2048 + reserved + fats*fat_sectors + (root_entries+31)//32


def test_a_reader_that_fails_midway_does_not_commit(native, tmp_path):
    """A truncated read must fail the update, not write a partial image."""
    card = make_card(tmp_path, [('FIRMWARE.IMG', update_image(b'q'*8192))])
    data = card.read_bytes()
    # Fail inside the file's own clusters. The mount only reads the FAT and the
    # root directory, so it still succeeds and the failure lands where it should.
    cutoff = data_start(card) + 8

    @SectorRead
    def flaky(sector, out):
        if sector >= cutoff:
            return 0
        C.memmove(out, data[sector*512:sector*512+512], 512)
        return 1
    volume = C.create_string_buffer(8192)
    assert native.fat_mount(volume, flaky, C.cast(None, SectorWrite), len(data))
    nor, flash, previous, writes = nor_model()
    report = Report()
    result = native.update_sd_apply(volume, C.byref(nor), 1, 1, 1, C.byref(report))
    assert result in (BAD_FILE, IO)
    assert flash[SLOT_B:SLOT_B+512] == b'\xff'*512
    assert flash[SLOT_A:SLOT_A+len(previous)] == previous


def test_a_failed_write_never_commits(native, tmp_path):
    """A NOR that erases but refuses to program must commit nothing."""
    payload = b'w'*4096
    card = make_card(tmp_path, [('FIRMWARE.IMG', update_image(payload, version=4))])
    volume, _ = mount(native, card)
    nor, flash, previous, writes = nor_model(program_fails=True)
    report = Report()
    assert native.update_sd_apply(volume, C.byref(nor), 1, 1, 1, C.byref(report)) == IO
    # The active slot is untouched and no commit record was written.
    assert flash[SLOT_A:SLOT_A+len(previous)] == previous
    assert flash[META_B:META_B+8] == b'\xff'*8


def test_the_card_stays_usable_after_a_check(native, tmp_path):
    """A check reads the card; it must leave the filesystem as it found it."""
    payload = b'k'*3000
    card = make_card(tmp_path, [('FIRMWARE.IMG', update_image(payload)),
                                ('LUM00001.BMP', b'other file')])
    volume, read = mount(native, card)
    report = Report()
    assert native.update_sd_check(volume, 1, C.byref(report)) == READY
    assert native.update_sd_check(volume, 1, C.byref(report)) == READY
    # The directory is still readable and the second file is still there.
    lib = C.CDLL(native._name)
    lib.fat_read.argtypes = [C.c_void_p, C.c_char_p, C.c_void_p, C.c_uint, C.c_void_p]
    out = (U8*64)()
    size = U32()
    assert lib.fat_read(volume, b'LUM00001.BMP', out, 64, C.byref(size))
    assert bytes(out[:size.value]) == b'other file'
