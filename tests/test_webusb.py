"""WebUSB: the descriptors a browser reads, and the update it can send.

The peer here plays the browser. It reads the BOS and the landing page, then
sends a container on the update endpoint and reads the status back through
control. So this is the protocol a browser client will speak, not a set of calls
into the engine's own helpers.

The firmware and theme paths are both covered, over a real FAT16 card: the
staged container is written to it, and the applied theme is read back from it.
"""
import ctypes as C
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT/'custom-firmware'
U8, U32 = C.c_ubyte, C.c_uint32

IDLE, STAGING, STAGED, APPLIED, FAILED, DENIED = range(6)
R_OK, R_BAD_CRC, R_MEDIUM, R_BAD_IMAGE, R_NO_WRITER, R_NOT_CONFIRMED, R_NOT_STAGED = range(7)
R_THEME_BASE, R_ENGINE_BASE = 16, 32
WEBUSB_FIRMWARE, WEBUSB_THEME = 1, 2
STATUS_MAGIC = 0x55534257
FILES_MAX = 4
HEADER_BYTES = 24
ENTRY_BYTES = 20
THEME_CONFIG_BYTES = 4096
BMP_BYTES = 54 + 320*240*3
SLOT_SIZE = 0x180000
IMAGE_HEADER = 64


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


card_tool = module('wu_card', FW/'tools/card.py')
sd_tool = module('wu_create_sd', ROOT/'tools/emu/create_sd.py')

SOURCES = ('storage.c','theme.c','theme_bundle.c','crc.c','update.c','update_sd.c','webusb.c')
PEER = FW/'tests/webusb_peer.c'


def build(tmp_path, define=()):
    if not shutil.which('cc'):
        pytest.skip('host C compiler missing')
    libpath = tmp_path/'webusb.so'
    subprocess.run(['cc','-std=gnu99','-O1','-g','-Wall','-Wextra','-Werror',
                    '-shared','-fPIC','-I'+str(FW/'include'), *define,
                    *(str(FW/'src'/s) for s in SOURCES), str(PEER),
                    '-o', str(libpath)], check=True)
    lib = C.CDLL(str(libpath))
    lib.fat_mount.argtypes = [C.c_void_p, SectorRead, SectorWrite, U32]
    lib.fat_replace.argtypes = [C.c_void_p, C.c_char_p, C.c_void_p, C.c_uint]
    lib.fat_read.argtypes = [C.c_void_p, C.c_char_p, C.c_void_p, C.c_uint, C.c_void_p]
    lib.bringup_h_webusb_init.argtypes = [C.c_void_p, U32]
    lib.bringup_h_webusb_feed.argtypes = [C.c_void_p, C.c_uint]
    lib.bringup_h_webusb_feed.restype = C.c_int
    lib.bringup_h_webusb_command.argtypes = [C.c_uint]
    lib.bringup_h_webusb_info.argtypes = [C.c_uint]
    lib.bringup_h_webusb_info.restype = C.c_uint
    lib.bringup_h_webusb_nor.argtypes = [C.c_uint]
    lib.bringup_h_webusb_nor.restype = C.c_uint
    for name in ('bos','url_desc'):
        getattr(lib,'bringup_h_webusb_'+name).argtypes = [C.c_void_p, C.c_uint]
        getattr(lib,'bringup_h_webusb_'+name).restype = C.c_uint
    lib.bringup_h_webusb_status_bytes.argtypes = [C.c_void_p, C.c_uint]
    lib.bringup_h_webusb_status_bytes.restype = C.c_uint
    lib.bringup_h_webusb_confirm.argtypes = [C.c_int]
    lib.bringup_h_webusb_recovery.argtypes = [C.c_int]
    lib.bringup_h_webusb_no_writer.argtypes = []
    lib.bringup_h_webusb_board.argtypes = [C.c_uint]
    return lib


SectorRead = C.CFUNCTYPE(C.c_int, U32, C.c_void_p)
SectorWrite = C.CFUNCTYPE(C.c_int, U32, C.c_void_p)
_callbacks = []


def make_card(tmp_path, entries=()):
    path = tmp_path/f'card-{len(list(tmp_path.glob("card-*.img")))}.img'
    sd_tool.create_image(path)
    if entries:
        card_tool.seed(path, entries)
    return path


def mount(lib, path):
    data = bytearray(path.read_bytes())

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
    _callbacks.append((read, write, data))
    assert lib.fat_mount(volume, read, write, len(data))
    return volume


def bmp(pixel=b'\x10\x20\x30'):
    size = BMP_BYTES
    header = bytearray(54)
    header[0:2] = b'BM'
    struct.pack_into('<I', header, 2, size)
    struct.pack_into('<I', header, 10, 54)
    struct.pack_into('<I', header, 14, 40)
    struct.pack_into('<I', header, 18, 320)
    struct.pack_into('<i', header, 22, 240)
    struct.pack_into('<H', header, 26, 1)
    struct.pack_into('<H', header, 28, 24)
    struct.pack_into('<I', header, 34, 320*240*3)
    return bytes(header)+pixel*320*240


INI = (b'background = #0c1218\npanel = #1a232c\ntext = #f1f4ed\nmuted = #96a7b2\n'
       b'accent = #a4edbf\nboot_ms = 900\nshutter_ms = 250\n'
       b'boot_image = BOOT.BMP\nbackground_image = BACK.BMP\n')


def image(payload, version=2, boards=7):
    header = bytearray(64)
    header[:8] = b'LUMAIMG\0'
    struct.pack_into('<HH8I', header, 8, 1, 64, boards, len(payload), 0x02000000,
                     0x02000000, version, zlib.crc32(payload), 0, 0)
    struct.pack_into('<I', header, 36, zlib.crc32(header))
    return bytes(header)+payload


def padded(name):
    """The container's own 8.3 form: space padded, uppercase, no dot."""
    stem, _, ext = name.partition('.')
    return (stem.ljust(8)+ext.ljust(3)).encode('ascii')


def container(kind, files):
    """The container a browser sends: header, entry table, then the bytes."""
    total = sum(len(data) for _, data in files)
    header = bytearray(HEADER_BYTES)
    header[:8] = b'LUMAUPD\x00'
    struct.pack_into('<HHHH', header, 8, 1, HEADER_BYTES, kind, len(files))
    struct.pack_into('<II', header, 16, total, zlib.crc32(b''.join(d for _, d in files)))
    table = bytearray()
    for name, data in files:
        entry = bytearray(ENTRY_BYTES)
        entry[0:11] = padded(name)
        struct.pack_into('<I', entry, 12, len(data))
        struct.pack_into('<I', entry, 16, zlib.crc32(data))
        table += entry
    return bytes(header)+bytes(table)+b''.join(d for _, d in files)


def browser_send(lib, blob, chunk=64):
    """What the browser does: BEGIN, then the container on the bulk endpoint."""
    lib.bringup_h_webusb_command(0x01)
    for at in range(0, len(blob), chunk):
        piece = blob[at:at+chunk]
        buf = (U8*len(piece)).from_buffer_copy(piece)
        if not lib.bringup_h_webusb_feed(buf, len(piece)):
            return False
    return True


def status(lib):
    buf = (U8*32)()
    n = lib.bringup_h_webusb_status_bytes(buf, 32)
    raw = bytes(buf[:n])
    assert n == 32 and struct.unpack_from('<I', raw, 0)[0] == STATUS_MAGIC
    return dict(zip(('state','result','kind','received','total','version','files'),
                    struct.unpack_from('<7I', raw, 4)))


def card_file(lib, volume, name, cap=1024*1024):
    out = (U8*cap)()
    size = U32()
    if not lib.fat_read(volume, name.encode(), out, cap, C.byref(size)):
        return None
    return bytes(out[:size.value])


def test_bos_carries_the_webusb_capability(tmp_path):
    lib = build(tmp_path)
    out = (U8*64)()
    n = lib.bringup_h_webusb_bos(out, 64)
    bos = bytes(out[:n])
    assert n == 29, 'BOS plus one platform capability'
    assert bos[0] == 5 and bos[1] == 0x0f
    assert struct.unpack_from('<H', bos, 2)[0] == 29
    assert bos[4] == 1
    # Platform capability descriptor.
    assert bos[5] == 24 and bos[6] == 0x10 and bos[7] == 0x05 and bos[8] == 0
    # The WebUSB UUID in little-endian byte order, which is what Chrome
    # compares against. Reversed, the capability is invisible with no error.
    assert bos[9:25] == bytes([0x38,0xB6,0x08,0x34,0xA9,0x09,0xA0,0x47,
                               0x8B,0xFD,0xA0,0x76,0x88,0x15,0xB6,0x65])
    assert struct.unpack_from('<H', bos, 25)[0] == 0x0100      # bcdVersion
    assert bos[27] == 0x01                                     # bVendorCode
    assert bos[28] == 0, 'no landing page is hosted yet, so none is claimed'


def test_landing_page_is_offered_when_one_is_configured(tmp_path):
    lib = build(tmp_path, ('-DWEBUSB_URL="https://example.test/luma"',))
    out = (U8*64)()
    n = lib.bringup_h_webusb_bos(out, 64)
    assert bytes(out[:n])[28] == 1, 'a configured URL means iLandingPage is set'
    m = lib.bringup_h_webusb_url_desc(out, 64)
    desc = bytes(out[:m])
    assert m == 3+len('example.test/luma')
    assert desc[1] == 0x03                    # URL descriptor
    assert desc[2] == 0x01                    # https, never plaintext http
    assert desc[3:].decode() == 'example.test/luma', 'the scheme is stripped'


def test_plaintext_landing_pages_are_refused(tmp_path):
    """http would let anyone on the path change the page that drives the device."""
    lib = build(tmp_path, ('-DWEBUSB_URL="http://example.test/luma"',))
    out = (U8*64)()
    assert lib.bringup_h_webusb_url_desc(out, 64) == 0


def test_an_empty_landing_page_is_not_offered(tmp_path):
    lib = build(tmp_path)
    out = (U8*64)()
    assert lib.bringup_h_webusb_url_desc(out, 64) == 0


def test_firmware_upload_stages_then_applies(tmp_path):
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    lib.bringup_h_webusb_board(1)
    lib.bringup_h_webusb_confirm(1)
    lib.bringup_h_webusb_recovery(1)
    payload = b'a brand new application'*100
    blob = container(WEBUSB_FIRMWARE, [('FIRMWARE.IMG', image(payload, version=9))])
    assert browser_send(lib, blob)
    staged = status(lib)
    assert (staged['state'], staged['kind'], staged['received']) == (STAGED, WEBUSB_FIRMWARE, len(blob)-HEADER_BYTES-ENTRY_BYTES)
    assert lib.bringup_h_webusb_nor(0x1a0000) == 0xff, 'nothing written yet'

    lib.bringup_h_webusb_command(0x02)         # COMMIT
    done = status(lib)
    assert (done['state'], done['result'], done['version']) == (APPLIED, R_OK, 9)
    # The inactive slot holds the payload, byte for byte.
    got = bytes(lib.bringup_h_webusb_nor(0x1a0000+i) for i in range(len(payload)))
    assert got == payload
    # And the active slot was not touched.
    assert bytes(lib.bringup_h_webusb_nor(0x20000+i) for i in range(20)) == b'an application that '


def test_firmware_needs_a_local_confirmation(tmp_path):
    """A browser must not be able to confirm its own update."""
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    lib.bringup_h_webusb_board(1)
    lib.bringup_h_webusb_recovery(1)           # recovery gate only
    blob = container(WEBUSB_FIRMWARE, [('FIRMWARE.IMG', image(b'x'*5000, version=9))])
    assert browser_send(lib, blob)
    lib.bringup_h_webusb_command(0x02)
    refused = status(lib)
    assert (refused['state'], refused['result']) == (DENIED, R_NOT_CONFIRMED)
    assert lib.bringup_h_webusb_info(10) == 0, 'nothing may have been programmed'


def test_firmware_without_a_writer_is_reported(tmp_path):
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    lib.bringup_h_webusb_board(1)
    lib.bringup_h_webusb_confirm(1)
    lib.bringup_h_webusb_recovery(1)
    lib.bringup_h_webusb_no_writer()
    assert browser_send(lib, container(WEBUSB_FIRMWARE, [('FIRMWARE.IMG', image(b'y'*3000))]))
    lib.bringup_h_webusb_command(0x02)
    assert status(lib)['result'] == R_NO_WRITER


def test_firmware_for_the_wrong_board_is_refused(tmp_path):
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    lib.bringup_h_webusb_board(1)
    lib.bringup_h_webusb_confirm(1)
    lib.bringup_h_webusb_recovery(1)
    blob = container(WEBUSB_FIRMWARE, [('FIRMWARE.IMG', image(b'z'*3000, version=9, boards=2))])
    assert browser_send(lib, blob)
    lib.bringup_h_webusb_command(0x02)
    assert status(lib)['result'] == R_BAD_IMAGE
    assert lib.bringup_h_webusb_info(10) == 0


def test_a_corrupted_transfer_never_reaches_the_flash(tmp_path):
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    lib.bringup_h_webusb_board(1)
    lib.bringup_h_webusb_confirm(1)
    lib.bringup_h_webusb_recovery(1)
    blob = bytearray(container(WEBUSB_FIRMWARE, [('FIRMWARE.IMG', image(b'w'*4000, version=9))]))
    blob[-1] ^= 1                              # one byte mangled on the way
    assert not browser_send(lib, blob)
    bad = status(lib)
    assert bad['state'] == FAILED and bad['result'] == R_BAD_CRC
    assert lib.bringup_h_webusb_nor(0x1a0000) == 0xff


def test_a_malformed_container_is_refused_before_the_card_is_touched(tmp_path):
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    good = container(WEBUSB_FIRMWARE, [('FIRMWARE.IMG', image(b'v'*3000))])
    cases = []
    bad = bytearray(good); bad[0] = ord('X'); cases.append(bytes(bad))
    bad = bytearray(good); struct.pack_into('<H', bad, 8, 2); cases.append(bytes(bad))
    bad = bytearray(good); struct.pack_into('<H', bad, 12, 9); cases.append(bytes(bad))
    bad = bytearray(good); struct.pack_into('<H', bad, 14, 0); cases.append(bytes(bad))
    bad = bytearray(good); struct.pack_into('<I', bad, 16, 0x7fffffff); cases.append(bytes(bad))
    for blob in cases:
        lib.bringup_h_webusb_command(0x01)
        buf = (U8*len(blob)).from_buffer_copy(blob)
        assert not lib.bringup_h_webusb_feed(buf, len(blob))
        assert status(lib)['state'] == STAGING, 'a refused container never stages'
        lib.bringup_h_webusb_command(0x03)     # ABORT clears it
        assert status(lib)['state'] == IDLE


def test_commit_without_a_staged_container_is_refused(tmp_path):
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    lib.bringup_h_webusb_confirm(1)
    lib.bringup_h_webusb_recovery(1)
    lib.bringup_h_webusb_command(0x02)
    refused = status(lib)
    assert (refused['state'], refused['result']) == (DENIED, R_NOT_STAGED)
    assert lib.bringup_h_webusb_info(10) == 0


def test_theme_upload_replaces_the_live_theme(tmp_path):
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    old = b'background = #0c1218\n'
    assert lib.fat_replace(volume, b'THEME.INI', old, len(old))
    boot = bmp(b'\x0a\x0b\x0c')
    back = bmp(b'\x0d\x0e\x0f')
    blob = container(WEBUSB_THEME, [('BOOT.BMP', boot), ('BACK.BMP', back), ('THEME.INI', INI)])
    assert browser_send(lib, blob)
    staged = status(lib)
    assert (staged['state'], staged['kind'], staged['files']) == (STAGED, WEBUSB_THEME, 3)
    assert card_file(lib, volume, 'THEME.INI', THEME_CONFIG_BYTES) == old, \
        'staging must not disturb the live theme'

    lib.bringup_h_webusb_command(0x02)
    done = status(lib)
    assert (done['state'], done['result']) == (APPLIED, R_OK)
    # All three files are live, byte for byte, and the config is the new one.
    assert card_file(lib, volume, 'BOOT.BMP') == boot
    assert card_file(lib, volume, 'BACK.BMP') == back
    assert card_file(lib, volume, 'THEME.INI', THEME_CONFIG_BYTES) == INI
    assert lib.bringup_h_webusb_info(13) == 0 and lib.bringup_h_webusb_info(14) == 0


@pytest.mark.parametrize('files,expected',[
    ([('THEME.INI', b'nonsense = 1\n')], R_THEME_BASE+2),
    ([('BOOT.BMP', None)], R_THEME_BASE+1),                      # no config at all
    ([('THEME.INI', INI)], R_THEME_BASE+3),                      # images missing
])
def test_a_bad_theme_bundle_is_refused(tmp_path, files, expected):
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    resolved = [(name, bmp() if data is None else data) for name, data in files]
    assert browser_send(lib, container(WEBUSB_THEME, resolved))
    lib.bringup_h_webusb_command(0x02)
    refused = status(lib)
    assert refused['state'] == FAILED and refused['result'] == expected


def test_a_theme_with_an_unreferenced_file_is_refused(tmp_path):
    """The theme path must not write arbitrary files to someone's card."""
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    blob = container(WEBUSB_THEME, [('BOOT.BMP', bmp()), ('BACK.BMP', bmp()),
                                    ('THEME.INI', INI), ('EVIL.BIN', b'payload')])
    assert browser_send(lib, blob)
    lib.bringup_h_webusb_command(0x02)
    refused = status(lib)
    assert (refused['state'], refused['result']) == (FAILED, R_THEME_BASE+5)
    assert card_file(lib, volume, 'EVIL.BIN', 64) is None


def test_a_theme_image_that_is_not_a_screen_is_refused(tmp_path):
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    blob = container(WEBUSB_THEME, [('BOOT.BMP', bmp()[:100]), ('BACK.BMP', bmp()),
                                    ('THEME.INI', INI)])
    assert browser_send(lib, blob)
    lib.bringup_h_webusb_command(0x02)
    assert status(lib)['result'] == R_THEME_BASE+4


def test_abort_leaves_the_card_as_it_was(tmp_path):
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    old = b'background = #0c1218\n'
    assert lib.fat_replace(volume, b'THEME.INI', old, len(old))
    blob = container(WEBUSB_THEME, [('BOOT.BMP', bmp()), ('THEME.INI', INI)])
    assert browser_send(lib, blob)
    lib.bringup_h_webusb_command(0x03)         # ABORT
    assert status(lib)['state'] == IDLE
    assert card_file(lib, volume, 'THEME.INI', THEME_CONFIG_BYTES) == old
    assert card_file(lib, volume, 'BOOT.BMP', 64) is None


def test_a_host_owned_card_cannot_be_staged_onto(tmp_path):
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    handoff = C.CDLL(lib._name).sd_handoff
    handoff.argtypes = [C.c_void_p, C.c_int, C.c_int]
    assert handoff(volume, 1, 1)               # the storage function owns it
    blob = container(WEBUSB_THEME, [('BOOT.BMP', bmp()), ('THEME.INI', INI)])
    assert not browser_send(lib, blob), 'a browser and the camera must not share one FAT'


def test_the_container_is_staged_under_its_own_name(tmp_path):
    """Staging must not clobber a good file with a half-received one."""
    lib = build(tmp_path)
    volume = mount(lib, make_card(tmp_path))
    lib.bringup_h_webusb_init(volume, 1)
    assert browser_send(lib, container(WEBUSB_THEME, [('BOOT.BMP', bmp()), ('THEME.INI', INI)]))
    staged = card_file(lib, volume, 'UPDATE.NEW')
    assert staged is not None and len(staged) > HEADER_BYTES
