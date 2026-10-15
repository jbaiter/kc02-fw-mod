"""Theme updates: a replaceable file, and a bundle that is validated as a set.

A theme is a config plus the images it names, so updating one means replacing
several files and doing it in an order that always leaves a theme that renders.
These tests use a real FAT16 card and real 320x240 BMPs, so the sizes and the
header checks are the ones the device actually applies.
"""
import ctypes as C
import importlib.util
from pathlib import Path
import struct
import shutil
import subprocess
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT/'custom-firmware'
U8, U32 = C.c_ubyte, C.c_uint32

BUNDLE_OK, MISSING_CONFIG, BAD_CONFIG, MISSING_IMAGE, BAD_IMAGE, UNREFERENCED, TOO_BIG, IO, BAD_NAME = range(9)
THEME_CONFIG_BYTES = 4096


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


card_tool = module('tb_card', FW/'tools/card.py')
sd_tool = module('tb_create_sd', ROOT/'tools/emu/create_sd.py')


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    if not shutil.which('cc'):
        pytest.skip('host C compiler missing')
    libpath = tmp_path_factory.mktemp('themebundle')/'bundle.so'
    subprocess.run(['cc','-std=gnu99','-O1','-g','-Wall','-Wextra','-Werror',
                    '-shared','-fPIC','-I'+str(FW/'include'),
                    str(FW/'src/theme_bundle.c'), str(FW/'src/storage.c'),
                    str(FW/'src/theme.c'), str(FW/'src/crc.c'),
                    '-o', str(libpath)], check=True)
    lib = C.CDLL(str(libpath))
    lib.fat_mount.argtypes = [C.c_void_p, SectorRead, SectorWrite, U32]
    lib.fat_replace.argtypes = [C.c_void_p, C.c_char_p, C.c_void_p, C.c_uint]
    lib.fat_replace.restype = C.c_int
    lib.fat_create.argtypes = [C.c_void_p, C.c_char_p, C.c_void_p, C.c_uint]
    lib.fat_create.restype = C.c_int
    lib.fat_read.argtypes = [C.c_void_p, C.c_char_p, C.c_void_p, C.c_uint, C.c_void_p]
    lib.fat_read.restype = C.c_int
    lib.fat_write_begin.argtypes = [C.c_void_p, C.c_char_p, U32, C.c_void_p]
    lib.fat_write_begin.restype = C.c_int
    lib.fat_write_data.argtypes = [C.c_void_p, C.c_void_p, C.c_uint]
    lib.fat_write_data.restype = C.c_int
    lib.fat_write_end.argtypes = [C.c_void_p]
    lib.fat_write_end.restype = C.c_int
    lib.fat_write_abort.argtypes = [C.c_void_p]
    lib.bmp_check.argtypes = [C.c_void_p, C.c_uint]
    lib.bmp_check.restype = C.c_int
    lib.theme_bundle_add.argtypes = [C.c_void_p, C.c_char_p, U32, U32]
    lib.theme_bundle_add.restype = C.c_int
    lib.theme_bundle_check.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
    lib.theme_bundle_check.restype = C.c_int
    lib.theme_bundle_commit.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
    lib.theme_bundle_commit.restype = C.c_int
    return lib


SectorRead = C.CFUNCTYPE(C.c_int, U32, C.c_void_p)
SectorWrite = C.CFUNCTYPE(C.c_int, U32, C.c_void_p)
BundleRead = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, U32, C.c_void_p, C.c_uint)
WriteBegin = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, U32, C.POINTER(C.c_void_p))
WriteData = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_uint)
WriteEnd = C.CFUNCTYPE(C.c_int, C.c_void_p)
WriteAbort = C.CFUNCTYPE(None, C.c_void_p)

FAT_WRITE_MAX = 512*1024
BMP_BYTES = 54 + 320*240*3
BW = 54
COUNTER = [0]


class Entry(C.Structure):
    _fields_ = [('name', C.c_char*13), ('size', U32), ('crc', U32)]


class Bundle(C.Structure):
    _fields_ = [('count', C.c_uint), ('bytes', U32), ('entry', Entry*4)]


class FatWrite(C.Structure):
    # The real struct is opaque to these tests; it is used through the API only.
    _fields_ = [('raw', C.c_ubyte*2048)]


class Source(C.Structure):
    _fields_ = [('context', C.c_void_p), ('read', BundleRead)]


class Writer(C.Structure):
    _fields_ = [('context', C.c_void_p), ('begin', WriteBegin), ('data', WriteData),
                ('end', WriteEnd), ('abort', WriteAbort)]


def make_card(tmp_path, entries=()):
    COUNTER[0] += 1
    image = tmp_path/f'card-{COUNTER[0]}.img'
    sd_tool.create_image(image)
    if entries:
        card_tool.seed(image, entries)
    return image


# ctypes collects a callback object when Python stops referencing it, even
# though C still holds its function pointer. The mount stores those pointers, so
# the callbacks must outlive this function or the next call jumps into freed
# memory. This is a test-harness hazard, not a firmware one.
_callbacks = {}


def mount(native, image, readonly=False):
    """A writable in-memory card, so a replaced file can be read back."""
    data = bytearray(image.read_bytes())

    @SectorRead
    def read(sector, out):
        if sector+1 > len(data)//512:
            return 0
        C.memmove(out, bytes(data[sector*512:sector*512+512]), 512)
        return 1

    @SectorWrite
    def write(sector, src):
        data[sector*512:sector*512+512] = C.string_at(src, 512)
        return 1
    volume = C.create_string_buffer(8192)
    _callbacks[id(volume)] = (read, write, data)
    assert native.fat_mount(volume, read, C.cast(None, SectorWrite) if readonly else write,
                            len(data))
    return volume


def bmp(pixel=b'\x10\x20\x30', height=240, width=320):
    """A real 320x240 24-bit BMP, bottom-up, as bmp_write produces."""
    size = 54 + width*height*3
    header = bytearray(54)
    header[0:2] = b'BM'
    struct.pack_into('<I', header, 2, size)
    struct.pack_into('<I', header, 10, 54)
    struct.pack_into('<I', header, 14, 40)
    struct.pack_into('<I', header, 18, width)
    struct.pack_into('<i', header, 22, height)
    struct.pack_into('<H', header, 26, 1)
    struct.pack_into('<H', header, 28, 24)
    struct.pack_into('<I', header, 34, width*height*3)
    rows = bytearray()
    for _ in range(height):
        rows += pixel*width
    assert len(rows) == width*height*3
    return bytes(header)+bytes(rows)


INI = (b'background = #0c1218\npanel = #1a232c\ntext = #f1f4ed\nmuted = #96a7b2\n'
       b'accent = #a4edbf\nboot_ms = 900\nshutter_ms = 250\n'
       b'boot_image = BOOT.BMP\nbackground_image = BACK.BMP\n')


def read_of(files):
    """A BundleSource over an in-memory set of staged files."""
    @BundleRead
    def read(ctx, name, offset, out, size):
        # ctypes hands a char* callback parameter over as bytes.
        key = bytes(name).decode('ascii').upper()
        data = files.get(key)
        if data is None or offset+size > len(data):
            return 0
        C.memmove(out, data[offset:offset+size], size)
        return 1
    return read


def source_of(files):
    """Keeps the callback alive: the Source holds only its function pointer."""
    callback = read_of(files)
    source = Source(None, callback)
    source._keepalive = callback
    return source


def add(native, bundle, name, data):
    assert native.theme_bundle_add(C.byref(bundle), name.encode(), len(data),
                                   zlib.crc32(data))


def test_fat_replace_creates_then_replaces(native, tmp_path):
    card = make_card(tmp_path)
    volume = mount(native, card)
    first = b'A'*1000
    second = b'B'*2000
    assert native.fat_replace(volume, b'THEME.INI', first, len(first))
    out = (U8*4096)()
    size = U32()
    assert native.fat_read(volume, b'THEME.INI', out, 4096, C.byref(size))
    assert size.value == len(first) and bytes(out[:size.value]) == first
    # Replacing is the case a create-only backend cannot do.
    assert native.fat_replace(volume, b'THEME.INI', second, len(second))
    assert native.fat_read(volume, b'THEME.INI', out, 4096, C.byref(size))
    assert size.value == len(second) and bytes(out[:size.value]) == second
    # Replacing with something shorter must not leave the old tail behind.
    third = b'C'*10
    assert native.fat_replace(volume, b'THEME.INI', third, len(third))
    assert native.fat_read(volume, b'THEME.INI', out, 4096, C.byref(size))
    assert size.value == len(third) and bytes(out[:size.value]) == third


def test_fat_create_still_refuses_an_existing_file(native, tmp_path):
    card = make_card(tmp_path)
    volume = mount(native, card)
    assert native.fat_create(volume, b'LUM00001.BMP', b'first', 5)
    # Captures are create-only: replacing a photo would be a bug.
    assert not native.fat_create(volume, b'LUM00001.BMP', b'second', 6)
    out = (U8*64)()
    size = U32()
    assert native.fat_read(volume, b'LUM00001.BMP', out, 64, C.byref(size))
    assert bytes(out[:size.value]) == b'first'


def test_fat_replace_streams_and_abort_leaves_the_old_file(native, tmp_path):
    card = make_card(tmp_path)
    volume = mount(native, card)
    original = bytes((i*7) & 0xff for i in range(3000))
    assert native.fat_replace(volume, b'THEME.INI', original, len(original))
    handle = FatWrite()
    assert native.fat_write_begin(volume, b'THEME.INI', 10000, C.byref(handle))
    for at in range(0, 4000, 500):
        chunk = bytes([0x5a])*500
        buf = (U8*500).from_buffer_copy(chunk)
        assert native.fat_write_data(C.byref(handle), buf, 500)
    native.fat_write_abort(C.byref(handle))
    out = (U8*4096)()
    size = U32()
    assert native.fat_read(volume, b'THEME.INI', out, 4096, C.byref(size))
    assert size.value == len(original) and bytes(out[:size.value]) == original, \
        'an abandoned upload must leave the old file exactly as it was'


def test_fat_replace_refuses_oversized_and_bad_names(native, tmp_path):
    card = make_card(tmp_path)
    volume = mount(native, card)
    assert not native.fat_replace(volume, b'THEME.INI', b'x', FAT_WRITE_MAX+1)
    for name in (b'lower.ini', b'toolongname.ini', b'bad-name.ini', b'.INI'):
        assert not native.fat_replace(volume, name, b'x', 1), name


def test_fat_replace_needs_ownership(native, tmp_path):
    """Only the local side may write: the card is not ours while a host has it."""
    lib = None
    card = make_card(tmp_path)
    volume = mount(native, card)
    lib = C.CDLL(native._name)
    lib.sd_handoff.argtypes = [C.c_void_p, C.c_int, C.c_int]
    assert lib.sd_handoff(volume, 1, 1)          # SD_HOST
    assert not native.fat_replace(volume, b'THEME.INI', b'x', 1)
    assert lib.sd_handoff(volume, 0, 1)
    assert not native.fat_replace(volume, b'THEME.INI', b'x', 1), 'mount invalidated'


def test_read_only_mount_cannot_be_written(native, tmp_path):
    card = make_card(tmp_path)
    volume = mount(native, card, readonly=True)
    assert not native.fat_replace(volume, b'THEME.INI', b'x', 1)


def test_bundle_accepts_a_consistent_theme(native):
    files = {'THEME.INI': INI, 'BOOT.BMP': bmp(b'\x01\x02\x03'),
             'BACK.BMP': bmp(b'\x04\x05\x06')}
    bundle = Bundle()
    for name, data in files.items():
        add(native, bundle, name, data)
    src = source_of(files)
    assert native.theme_bundle_check(C.byref(bundle), C.byref(src), None) == BUNDLE_OK


def test_bundle_without_a_config_is_refused(native):
    files = {'BOOT.BMP': bmp()}
    bundle = Bundle()
    add(native, bundle, 'BOOT.BMP', files['BOOT.BMP'])
    src = source_of(files)
    assert native.theme_bundle_check(C.byref(bundle), C.byref(src), None) == MISSING_CONFIG


def test_bundle_config_must_parse(native):
    files = {'THEME.INI': b'accent = #gg0000\n', 'BOOT.BMP': bmp()}
    bundle = Bundle()
    for name, data in files.items():
        add(native, bundle, name, data)
    src = source_of(files)
    assert native.theme_bundle_check(C.byref(bundle), C.byref(src), None) == BAD_CONFIG


def test_bundle_must_supply_the_images_the_config_names(native):
    files = {'THEME.INI': INI, 'BOOT.BMP': bmp()}      # BACK.BMP missing
    bundle = Bundle()
    for name, data in files.items():
        add(native, bundle, name, data)
    src = source_of(files)
    assert native.theme_bundle_check(C.byref(bundle), C.byref(src), None) == MISSING_IMAGE


def test_bundle_rejects_an_image_that_is_not_a_screen(native):
    # Wrong dimensions, wrong depth, and a truncated file. The first two are
    # rejected on their headers; the last fails the declared length check.
    for bad in (bmp(height=120), bmp(width=200, height=200), bmp()[:100],
                b'BM'+b'\x00'*98):
        files = {'THEME.INI': INI, 'BOOT.BMP': bad, 'BACK.BMP': bmp()}
        bundle = Bundle()
        for name, data in files.items():
            add(native, bundle, name, data)
        src = source_of(files)
        assert native.theme_bundle_check(C.byref(bundle), C.byref(src), None) == BAD_IMAGE


def test_bundle_rejects_a_corrupted_transfer(native):
    """The declared CRC is what catches damage between the host and the card.

    A BMP with valid headers and matching bytes is a legitimate image, so the
    CRC is the only thing that can detect a transport that mangled it. Here the
    bundle declares the good CRC and the staged file is not the good file.
    """
    good = bmp()
    corrupt = bytearray(good)
    corrupt[5000] ^= 1                  # right size, right header, wrong bytes
    files = {'THEME.INI': INI, 'BOOT.BMP': bytes(corrupt), 'BACK.BMP': bmp()}
    bundle = Bundle()
    add(native, bundle, 'THEME.INI', INI)
    add(native, bundle, 'BOOT.BMP', good)       # CRC of the good bytes
    add(native, bundle, 'BACK.BMP', files['BACK.BMP'])
    src = source_of(files)
    assert native.theme_bundle_check(C.byref(bundle), C.byref(src), None) == BAD_IMAGE


def test_bundle_accepts_a_well_formed_image_it_cannot_decode(native):
    """Header plus CRC is the contract; the pixels are the theme's business."""
    image = bytearray(bmp())
    image[5000] ^= 1
    files = {'THEME.INI': INI, 'BOOT.BMP': bytes(image), 'BACK.BMP': bmp()}
    bundle = Bundle()
    for name, data in files.items():
        add(native, bundle, name, data)
    src = source_of(files)
    assert native.theme_bundle_check(C.byref(bundle), C.byref(src), None) == BUNDLE_OK


def test_bundle_refuses_files_the_config_does_not_use(native):
    """The theme path must not become a way to write arbitrary files.

    The extra file is small, so the bundle is well inside its size limit and the
    refusal is about the reference, not the total.
    """
    files = {'THEME.INI': INI, 'BOOT.BMP': bmp(), 'BACK.BMP': bmp(),
             'NOTES.TXT': b'just some notes'}
    bundle = Bundle()
    for name, data in files.items():
        add(native, bundle, name, data)
    src = source_of(files)
    assert native.theme_bundle_check(C.byref(bundle), C.byref(src), None) == UNREFERENCED


def test_bundle_limits(native):
    files = {'THEME.INI': INI, 'BOOT.BMP': bmp(), 'BACK.BMP': bmp()}
    bundle = Bundle()
    for name, data in files.items():
        add(native, bundle, name, data)
    bundle.bytes = 512*1024+1
    src = source_of(files)
    assert native.theme_bundle_check(C.byref(bundle), C.byref(src), None) == TOO_BIG
    # A single file over the per-file limit cannot even be added.
    assert not native.theme_bundle_add(C.byref(bundle), b'HUGE.BMP',
                                       256*1024+1, 0)
    # More files than a theme can have.
    bundle2 = Bundle()
    for name in ('A.BMP','B.BMP','C.BMP','D.BMP'):
        native.theme_bundle_add(C.byref(bundle2), name.encode(), 1, 0)
    assert not native.theme_bundle_add(C.byref(bundle2), b'E.BMP', 1, 0)


def test_bundle_add_normalises_and_replaces_entries(native):
    bundle = Bundle()
    add(native, bundle, 'theme.ini', INI)
    assert bytes(bundle.entry[0].name).rstrip(b'\x00') == b'THEME   INI'
    assert bundle.count == 1 and bundle.bytes == len(INI)
    # The same file again replaces its entry rather than adding one.
    assert native.theme_bundle_add(C.byref(bundle), b'THEME.INI', 10, 0)
    assert bundle.count == 1 and bundle.bytes == 10


def test_commit_writes_images_before_the_config(native):
    """Order matters: an interruption must leave a theme that still renders."""
    files = {'THEME.INI': INI, 'BOOT.BMP': bmp(), 'BACK.BMP': bmp()}
    bundle = Bundle()
    for name, data in files.items():
        add(native, bundle, name, data)
    src = source_of(files)
    order = []
    written = {}

    @WriteBegin
    def begin(ctx, name, size, handle):
        order.append(bytes(name).decode())
        return 1

    @WriteData
    def data(handle, buf, size):
        return 1

    @WriteEnd
    def end(handle):
        return 1

    @WriteAbort
    def abort(handle):
        pass
    writer = Writer(None, begin, data, end, abort)
    assert native.theme_bundle_commit(C.byref(bundle), C.byref(src),
                                      C.byref(writer)) == BUNDLE_OK
    assert order[0] in ('BOOT.BMP', 'BACK.BMP')
    assert order[-1] == 'THEME.INI'
    assert sorted(order) == ['BACK.BMP', 'BOOT.BMP', 'THEME.INI']


def test_commit_reports_a_write_failure(native):
    files = {'THEME.INI': INI, 'BOOT.BMP': bmp(), 'BACK.BMP': bmp()}
    bundle = Bundle()
    for name, data in files.items():
        add(native, bundle, name, data)
    src = source_of(files)

    @WriteBegin
    def begin(ctx, name, size, handle):
        return 1

    @WriteData
    def data(handle, buf, size):
        return 0                     # refuse the first write

    @WriteEnd
    def end(handle):
        return 1

    @WriteAbort
    def abort(handle):
        pass
    writer = Writer(None, begin, data, end, abort)
    assert native.theme_bundle_commit(C.byref(bundle), C.byref(src),
                                      C.byref(writer)) == IO


def test_commit_streams_whole_files_to_a_real_card(native, tmp_path):
    """The commit path over a real card, through the real streaming writer.

    The handle the bundle hands to `data` and `end` is a live FatWrite, exactly
    as it is on the device.
    """
    files = {'THEME.INI': INI, 'BOOT.BMP': bmp(b'\x0a\x0b\x0c'),
             'BACK.BMP': bmp(b'\x0d\x0e\x0f')}
    bundle = Bundle()
    for name, data_ in files.items():
        add(native, bundle, name, data_)
    card = make_card(tmp_path)
    volume = mount(native, card)
    written = {}

    @WriteBegin
    def begin(ctx, name, size, handle):
        key = bytes(name).decode()
        holder = FatWrite()
        if not native.fat_write_begin(volume, name, size, C.byref(holder)):
            return 0
        written[key] = holder               # keeps the struct alive
        handle[0] = C.addressof(holder)
        return 1

    @WriteData
    def data(handle, buf, size):
        holder = C.cast(handle, C.POINTER(FatWrite)).contents
        return native.fat_write_data(C.byref(holder), buf, size)

    @WriteEnd
    def end(handle):
        holder = C.cast(handle, C.POINTER(FatWrite)).contents
        return native.fat_write_end(C.byref(holder))

    @WriteAbort
    def abort(handle):
        holder = C.cast(handle, C.POINTER(FatWrite)).contents
        native.fat_write_abort(C.byref(holder))

    order = []
    real_begin = begin

    @WriteBegin
    def recording_begin(ctx, name, size, handle):
        order.append(bytes(name).decode())
        return real_begin(ctx, name, size, handle)
    writer = Writer(None, recording_begin, data, end, abort)
    assert native.theme_bundle_commit(C.byref(bundle), C.byref(source_of(files)),
                                      C.byref(writer)) == BUNDLE_OK
    assert order[-1] == 'THEME.INI', 'the config must be written last'
    assert sorted(order) == ['BACK.BMP', 'BOOT.BMP', 'THEME.INI']

    # Every file is on the card byte for byte, and the config parses from it.
    out = (U8*BMP_BYTES)()
    size = U32()
    for name, payload in files.items():
        assert native.fat_read(volume, name.encode(), out, BMP_BYTES, C.byref(size))
        assert size.value == len(payload)
        assert bytes(out[:size.value]) == payload
    lib = C.CDLL(native._name)
    lib.theme_parse.argtypes = [C.c_void_p, C.c_void_p, U32]
    lib.theme_parse.restype = C.c_int
    text = (U8*THEME_CONFIG_BYTES)()
    assert native.fat_read(volume, b'THEME.INI', text, THEME_CONFIG_BYTES, C.byref(size))
    theme = C.create_string_buffer(64)
    assert lib.theme_parse(theme, text, size.value)


def test_commit_to_a_real_card_aborts_cleanly(native, tmp_path):
    """A refused write leaves the old theme in place, not a half-replaced one."""
    old = b'background = #0c1218\n'
    card = make_card(tmp_path, [('THEME.INI', old)])
    volume = mount(native, card)
    files = {'THEME.INI': INI, 'BOOT.BMP': bmp()}
    bundle = Bundle()
    for name, data_ in files.items():
        add(native, bundle, name, data_)
    written = {}
    fail = [False]

    @WriteBegin
    def begin(ctx, name, size, handle):
        holder = FatWrite()
        if not native.fat_write_begin(volume, name, size, C.byref(holder)):
            return 0
        written[bytes(name).decode()] = holder
        handle[0] = C.addressof(holder)
        return 1

    @WriteData
    def data(handle, buf, size):
        if fail[0]:
            return 0
        holder = C.cast(handle, C.POINTER(FatWrite)).contents
        return native.fat_write_data(C.byref(holder), buf, size)

    @WriteEnd
    def end(handle):
        holder = C.cast(handle, C.POINTER(FatWrite)).contents
        return native.fat_write_end(C.byref(holder))

    @WriteAbort
    def abort(handle):
        holder = C.cast(handle, C.POINTER(FatWrite)).contents
        native.fat_write_abort(C.byref(holder))
    # Let the image through, then refuse the config: the old config survives.
    @WriteBegin
    def begin_image_only(ctx, name, size, handle):
        fail[0] = bytes(name).decode() == 'THEME.INI'
        return begin(ctx, name, size, handle)
    writer = Writer(None, begin_image_only, data, end, abort)
    assert native.theme_bundle_commit(C.byref(bundle), C.byref(source_of(files)),
                                      C.byref(writer)) == IO
    out = (U8*THEME_CONFIG_BYTES)()
    size = U32()
    assert native.fat_read(volume, b'THEME.INI', out, THEME_CONFIG_BYTES, C.byref(size))
    assert bytes(out[:size.value]) == old, 'the old config must be untouched'


def test_bmp_check_rejects_what_bmp_read_rejects(native):
    good = bmp()
    assert native.bmp_check((U8*len(good)).from_buffer_copy(good), len(good))
    for bad in (good[:53], good[:100], b'BM'+b'\x11'*98,
                good[:2]+struct.pack('<I', len(good)+10)+good[6:]):
        buf = (U8*len(bad)).from_buffer_copy(bad)
        assert not native.bmp_check(buf, len(bad))
