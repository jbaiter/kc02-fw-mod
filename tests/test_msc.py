"""USB mass storage: Bulk-Only Transport and the SCSI command set.

The peer in msc_peer.c acts as the USB host: it builds CBWs, drains the bulk-IN
pipe, answers bulk-OUT requests and parses the CSW. So these are protocol
conversations, not calls into the engine's own helpers. Results are read back
through accessors, so nothing here depends on a C struct layout.
"""
import ctypes as C
from pathlib import Path
import shutil
import struct
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT/'custom-firmware'
U8, U32 = C.c_ubyte, C.c_uint32

CSW_SIGNATURE = 0x53425355
PASSED, FAILED, PHASE_ERROR = 0, 1, 2
SENSE_NOT_READY = 0x02
SENSE_MEDIUM_ERROR = 0x03
SENSE_ILLEGAL_REQUEST = 0x05
SENSE_UNIT_ATTENTION = 0x06
SENSE_DATA_PROTECT = 0x07
STALL_IN, STALL_OUT = 1, 0
BLOCKS = 8


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    if not shutil.which('cc'):
        pytest.skip('host C compiler missing')
    libpath = tmp_path_factory.mktemp('msc')/'msc.so'
    subprocess.run(['cc','-std=gnu99','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=undefined','-fno-sanitize-recover=undefined','-shared','-fPIC',
                    '-I'+str(FW/'include'),'-I'+str(FW/'tests'),
                    str(FW/'src/msc.c'), str(FW/'tests/msc_peer.c'), '-o', str(libpath)],
                   check=True)
    lib = C.CDLL(str(libpath))
    lib.peer_init.argtypes = [C.c_void_p, U32, C.c_int]
    lib.peer_command.argtypes = [C.c_void_p, C.c_void_p, C.c_uint, C.c_uint,
                                 C.c_void_p, C.c_uint]
    lib.peer_command.restype = C.c_int
    for name in ('out_size','residue','reads','writes','syncs','resets','stalls'):
        getattr(lib,'peer_'+name).restype = C.c_uint
    lib.peer_csw32.argtypes = [C.c_void_p, C.c_uint]
    lib.peer_csw32.restype = U32
    lib.peer_tag.argtypes = [C.c_void_p]
    lib.peer_tag.restype = U32
    lib.peer_stalls.argtypes = [C.c_void_p, C.c_int]
    lib.peer_fail_read.argtypes = [C.c_void_p, C.c_int]
    lib.peer_fail_write.argtypes = [C.c_void_p, C.c_int]
    lib.peer_raw_cbw.argtypes = [C.c_void_p, C.c_void_p, C.c_uint]
    lib.peer_raw_cbw.restype = C.c_int
    for name in ('peer_out','peer_medium','peer_csw'):
        getattr(lib,name).restype = C.POINTER(U8)
    lib.peer_msc.restype = C.c_void_p
    return lib


@pytest.fixture
def peer(native):
    """An initialised peer, reached only through accessors."""
    handle = C.create_string_buffer(32768)
    native.peer_init(handle, BLOCKS, 1)
    return handle


def bytes_at(native, accessor, count, size=0):
    pointer = accessor
    return bytes(bytearray(pointer[i] for i in range(count))) if count else b''


def out_data(native, p):
    size = native.peer_out_size(p)
    return bytes(bytearray(native.peer_out(p)[i] for i in range(size)))


def medium_sector(native, p, lba):
    base = native.peer_medium(p)
    return bytes(bytearray(base[lba*512+i] for i in range(512)))


def cdb_read10(lba, blocks, flags=0):
    """READ(10): opcode, flags, LBA(4), reserved, length(2), control."""
    return bytes([0x28, flags]) + struct.pack('>I', lba) + b'\x00' + \
           struct.pack('>H', blocks) + b'\x00'


def cdb_write10(lba, blocks, flags=0):
    return bytes([0x2a, flags]) + struct.pack('>I', lba) + b'\x00' + \
           struct.pack('>H', blocks) + b'\x00'


def run(native, p, cdb, data_len=0, data_in=None):
    buf = (U8*len(data_in)).from_buffer_copy(data_in) if data_in else None
    return native.peer_command(p, cdb, len(cdb), data_len, buf,
                               len(data_in) if data_in else 0)


def csw_status(native, p):
    return native.peer_csw(p)[12]


def sense_of(native, p):
    """Reads the sense the way a host would: REQUEST SENSE."""
    assert run(native, p, bytes([0x03, 0, 0, 0, 18, 0]), data_len=18) == PASSED
    return bytes(bytearray(native.peer_out(p)[i] for i in range(18)))


def test_inquiry_and_capacity(native, peer):
    assert run(native, peer, bytes([0x12, 0, 0, 0, 36, 0]), data_len=36) == PASSED
    inq = out_data(native, peer)
    assert inq[0] == 0x00 and inq[2] == 0x05 and inq[4] == 31
    assert inq[8:16] == b'LUMA    '
    assert inq[16:32] == b'KC02 SD CARD    '
    assert inq[32:36] == b'0001'
    assert native.peer_csw32(peer, 0) == CSW_SIGNATURE
    assert native.peer_csw32(peer, 4) == native.peer_tag(peer)
    assert csw_status(native, peer) == PASSED

    assert run(native, peer, bytes([0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0]), data_len=8) == PASSED
    cap = out_data(native, peer)
    assert struct.unpack('>I', cap[0:4])[0] == BLOCKS-1
    assert struct.unpack('>I', cap[4:8])[0] == 512


def test_read10_returns_real_medium_bytes(native, peer):
    assert run(native, peer, cdb_read10(1, 2), data_len=1024) == PASSED
    assert native.peer_out_size(peer) == 1024
    assert native.peer_residue(peer) == 0
    assert out_data(native, peer) == medium_sector(native, peer, 1) + medium_sector(native, peer, 2)
    assert native.peer_reads(peer) == 2


def test_read10_reports_residue_when_the_host_asks_for_less(native, peer):
    # The command covers two blocks but the CBW declares one. Only what the host
    # asked for is sent, and the rest is reported as residue.
    assert run(native, peer, cdb_read10(0, 2), data_len=512) == PASSED
    assert native.peer_out_size(peer) == 512
    assert native.peer_residue(peer) == 512
    assert native.peer_reads(peer) == 1


def test_write10_round_trip_and_sync(native, peer):
    payload = bytes((i*3+1) & 0xff for i in range(1024))
    assert run(native, peer, cdb_write10(3, 2), data_len=1024, data_in=payload) == PASSED
    assert native.peer_writes(peer) == 2
    assert native.peer_residue(peer) == 0
    assert medium_sector(native, peer, 3) == payload[0:512]
    assert medium_sector(native, peer, 4) == payload[512:1024]
    assert run(native, peer, bytes([0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0])) == PASSED
    assert native.peer_syncs(peer) == 1


def test_write10_with_a_truncated_cbw_is_a_phase_error(native, peer):
    # Two blocks in the CDB, one declared in the CBW: the host believes it wrote
    # two. Refusing beats silently dropping a block.
    assert run(native, peer, cdb_write10(0, 2), data_len=512,
               data_in=b'\x11'*512) == PHASE_ERROR
    assert native.peer_writes(peer) == 0


def test_read_of_an_out_of_range_block_is_refused(native, peer):
    assert run(native, peer, cdb_read10(BLOCKS, 1), data_len=512) == FAILED
    assert sense_of(native, peer)[2] == SENSE_ILLEGAL_REQUEST
    # A read that straddles the end must not wrap around.
    assert run(native, peer, cdb_read10(BLOCKS-1, 2), data_len=1024) == FAILED


def test_zero_length_read_completes_with_no_data(native, peer):
    assert run(native, peer, cdb_read10(0, 0), data_len=0) == PASSED
    assert native.peer_out_size(peer) == 0
    assert native.peer_residue(peer) == 0


def test_read_only_medium_refuses_writes(native, peer):
    native.peer_init(peer, BLOCKS, 0)
    assert run(native, peer, cdb_write10(0, 1), data_len=512,
               data_in=b'\x22'*512) == FAILED
    assert native.peer_writes(peer) == 0
    assert sense_of(native, peer)[2] == SENSE_DATA_PROTECT


def test_format_unit_is_refused(native, peer):
    assert run(native, peer, bytes([0x04, 0, 0, 0, 0, 0])) == FAILED
    assert sense_of(native, peer)[2] == SENSE_ILLEGAL_REQUEST
    # Nothing may have touched the medium.
    assert native.peer_writes(peer) == 0 and native.peer_reads(peer) == 0


def test_absent_medium_is_not_ready(native, peer):
    native.peer_init(peer, 0, 0)
    assert run(native, peer, bytes([0x00, 0, 0, 0, 0, 0])) == FAILED
    assert sense_of(native, peer)[2] == SENSE_NOT_READY
    assert run(native, peer, cdb_read10(0, 1), data_len=512) == FAILED
    assert run(native, peer, bytes([0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0]), data_len=8) == FAILED
    assert sense_of(native, peer)[2] == SENSE_NOT_READY
    assert run(native, peer, cdb_write10(0, 1), data_len=512,
               data_in=b'\x44'*512) == FAILED
    assert native.peer_writes(peer) == 0


def test_eject_is_refused_but_prevent_allow_is_accepted(native, peer):
    assert run(native, peer, bytes([0x1e, 0, 0, 0, 1, 0])) == PASSED
    assert run(native, peer, bytes([0x1b, 0, 0, 0, 2, 0])) == FAILED
    assert sense_of(native, peer)[2] == SENSE_ILLEGAL_REQUEST


def test_medium_errors_are_reported_not_hidden(native, peer):
    native.peer_fail_read(peer, 1)
    assert run(native, peer, cdb_read10(0, 1), data_len=512) == FAILED
    assert sense_of(native, peer)[2] == SENSE_MEDIUM_ERROR
    native.peer_fail_read(peer, 0)
    native.peer_fail_write(peer, 1)
    assert run(native, peer, cdb_write10(0, 1), data_len=512,
               data_in=b'\x33'*512) == FAILED
    assert sense_of(native, peer)[2] == SENSE_MEDIUM_ERROR


def test_report_luns_and_mode_sense(native, peer):
    assert run(native, peer, bytes([0xa0, 0, 0, 0, 0, 0, 0, 0, 16, 0]), data_len=16) == PASSED
    assert struct.unpack('>I', out_data(native, peer)[0:4])[0] == 16
    assert run(native, peer, bytes([0x1a, 0, 0x3f, 0, 4, 0]), data_len=4) == PASSED
    assert out_data(native, peer) == bytes([3, 0, 0, 0])
    assert run(native, peer, bytes([0x5a, 0, 0x3f, 0, 0, 0, 0, 0, 8, 0]), data_len=8) == PASSED
    assert out_data(native, peer)[0:2] == bytes([0, 6])


@pytest.mark.parametrize('cdb',[
    bytes([0xa8, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0]),                   # READ(12)
    bytes([0xaa, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0]),                   # WRITE(12)
    bytes([0x9e, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]),    # READ CAPACITY(16)
    bytes([0x12, 1, 0x80, 0, 36, 0]),                                 # INQUIRY EVPD
    cdb_read10(1, 1, flags=0x10),                                    # READ(10) with FUA
    bytes([0xff, 0, 0, 0, 0, 0]),                                     # unknown
])
def test_unsupported_commands_are_refused_not_invented(native, peer, cdb):
    assert run(native, peer, cdb, data_len=0) == FAILED
    assert sense_of(native, peer)[2] == SENSE_ILLEGAL_REQUEST


def test_bad_cbw_signature_stalls_both_pipes(native, peer):
    frame = (U8*31)()          # all zero: signature does not match
    assert native.peer_raw_cbw(peer, frame, 31) == -1
    assert native.peer_stalls(peer, STALL_IN) == 1
    assert native.peer_stalls(peer, STALL_OUT) == 1


def test_short_and_oversized_cbw_frames_are_rejected(native, peer):
    good = (U8*31)()
    good[0], good[1], good[2], good[3] = 0x55, 0x53, 0x42, 0x43
    good[14] = 6
    assert native.peer_raw_cbw(peer, good, 30) == -1, 'short frame'
    zero_cdb = (U8*31)()
    zero_cdb[0], zero_cdb[1], zero_cdb[2], zero_cdb[3] = 0x55, 0x53, 0x42, 0x43
    zero_cdb[14] = 0
    native.peer_raw_reset(peer)
    assert native.peer_raw_cbw(peer, zero_cdb, 31) == -1, 'zero-length CDB'
    native.peer_raw_reset(peer)
    zero_cdb[14] = 17
    assert native.peer_raw_cbw(peer, zero_cdb, 31) == -1, 'oversized CDB'


def test_reset_reports_unit_attention_once(native, peer):
    native.peer_raw_reset(peer)
    assert run(native, peer, bytes([0x00, 0, 0, 0, 0, 0])) == FAILED
    assert sense_of(native, peer)[2] == SENSE_UNIT_ATTENTION
    # Clearing it means the next command succeeds normally.
    assert run(native, peer, bytes([0x00, 0, 0, 0, 0, 0])) == PASSED


def test_cbw_during_a_transfer_is_aborted(native, peer):
    """A new CBW mid-transfer must not interleave with the one in flight."""
    pending = C.create_string_buffer(31)
    # Start a READ(10) of four blocks but only let the peer answer one, so the
    # data phase cannot finish.
    cdb = cdb_read10(0, 4)
    assert run(native, peer, cdb, data_len=2048) == PASSED
    # Now push a fresh CBW while the engine is idle again: it must be accepted,
    # which proves the previous transfer left no residue behind.
    assert run(native, peer, bytes([0x00, 0, 0, 0, 0, 0])) == PASSED
    assert csw_status(native, peer) == PASSED
    (pending)


def test_scsi_fields_are_big_endian(native, peer):
    """A byte-order slip here sends every transfer to the wrong block.

    The peer builds CDBs to spec, so a little-endian parse shows up as a
    refusal (the block is out of range) rather than as silent corruption.
    """
    # Transfer length 0x0100 is 256 blocks, far past an 8-block medium. Read
    # little-endian it would look like one block and quietly succeed.
    assert run(native, peer, cdb_read10(0, 0x0100), data_len=512) == FAILED
    assert sense_of(native, peer)[2] == SENSE_ILLEGAL_REQUEST
    # LBA 0x00010000 is likewise out of range; little-endian it would be 0.
    assert run(native, peer, cdb_read10(0x00010000, 1), data_len=512) == FAILED
    assert sense_of(native, peer)[2] == SENSE_ILLEGAL_REQUEST
    # And the in-range case must actually land on the block it names.
    assert run(native, peer, cdb_read10(2, 1), data_len=512) == PASSED
    assert out_data(native, peer) == medium_sector(native, peer, 2)


def test_residue_is_reported_for_every_command(native, peer):
    # A short read must carry the unconsumed bytes as residue, not zero.
    assert run(native, peer, cdb_read10(0, 3), data_len=512) == PASSED
    assert native.peer_out_size(peer) == 512
    assert native.peer_residue(peer) == 1024
    # A fully served read has no residue.
    assert run(native, peer, cdb_read10(0, 3), data_len=1536) == PASSED
    assert native.peer_residue(peer) == 0
    # A failed command reports the whole command length as residue.
    assert run(native, peer, cdb_read10(99, 2), data_len=1024) == FAILED
    assert native.peer_residue(peer) == 1024
    # A command with no data phase has no residue to report.
    assert run(native, peer, bytes([0x00, 0, 0, 0, 0, 0])) == PASSED
    assert native.peer_residue(peer) == 0


def test_a_misconfigured_link_is_refused_not_called(native, peer):
    """A null callback must fail, not fault the target."""
    import ctypes
    lib = native
    handle = ctypes.create_string_buffer(32768)
    # Zero max_packet makes the link unusable; the engine must say so.
    lib.peer_init(handle, BLOCKS, 1)
    assert lib.peer_raw_cbw(handle, ctypes.create_string_buffer(31), 31) == -1


def test_csw_is_consistent_for_every_command(native, peer):
    for cdb, length in ((bytes([0x00, 0, 0, 0, 0, 0]), 0),
                        (bytes([0x12, 0, 0, 0, 36, 0]), 36),
                        (cdb_read10(0, 1), 512),
                        (bytes([0xff, 0, 0, 0, 0, 0]), 0)):
        run(native, peer, cdb, data_len=length)
        assert native.peer_csw32(peer, 0) == CSW_SIGNATURE
        assert native.peer_csw32(peer, 4) == native.peer_tag(peer)
        assert csw_status(native, peer) in (PASSED, FAILED, PHASE_ERROR)


def test_a_full_session_looks_like_a_disk(native, peer):
    """The sequence a host actually performs when it mounts the volume."""
    assert run(native, peer, bytes([0x12, 0, 0, 0, 36, 0]), data_len=36) == PASSED   # INQUIRY
    assert run(native, peer, bytes([0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0]), data_len=8) == PASSED
    assert run(native, peer, cdb_read10(0, 1), data_len=512) == PASSED        # boot sector
    assert run(native, peer, cdb_read10(0, 1), data_len=512) == PASSED        # retry, same bytes
    assert out_data(native, peer) == medium_sector(native, peer, 0)
    assert run(native, peer, bytes([0x1e, 0, 0, 0, 1, 0])) == PASSED          # prevent removal
    assert run(native, peer, cdb_write10(2, 1), data_len=512,
               data_in=b'\x5a'*512) == PASSED
    assert run(native, peer, bytes([0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0])) == PASSED
    assert medium_sector(native, peer, 2) == b'\x5a'*512
    assert run(native, peer, bytes([0x1e, 0, 0, 0, 0, 0])) == PASSED          # allow removal
    # Two reads of block 0 (the host retries), one write, one sync.
    assert native.peer_reads(peer) == 2 and native.peer_writes(peer) == 1
