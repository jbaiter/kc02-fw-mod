"""Recovery/debug bring-up: offline layout, USB contracts, monitor access policy.

The register model here is not physical USB validation. QEMU tests live in the
opt-in bringup_smoke.py runner because the USB controller is not emulated there.
"""
import ctypes as C
import hashlib
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
FW = ROOT/'custom-firmware'
U8, U32 = C.c_ubyte, C.c_uint32


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


factory = module('luma_factory', FW/'tools/factory.py')
bridge = module('luma_usb_gdb', FW/'tools/usb_gdb.py')


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    if not shutil.which('cc'):
        pytest.skip('host C compiler missing')
    libpath = tmp_path_factory.mktemp('bringup')/'bringup.so'
    subprocess.run(['cc','-std=c99','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=undefined','-fno-sanitize-recover=undefined','-shared','-fPIC',
                    '-I'+str(FW/'include'),
                    *(str(FW/'src'/s) for s in ('debug.c','monitor.c','usb_debug.c','ax_usb.c','destbin.c',
                                                   'storage.c','recover.c')),
                    str(FW/'tests/bringup_harness.c'),'-o',str(libpath)],check=True)
    lib = C.CDLL(str(libpath))
    lib.bringup_h_setup.argtypes = [C.c_void_p,C.c_void_p]
    lib.bringup_h_state.argtypes = [C.c_uint]
    for name in ('feed','queue','ax_feed'):
        getattr(lib,'bringup_h_'+name).argtypes = [C.c_void_p,C.c_uint]
    for name in ('take','ax_take','ax_setup'):
        getattr(lib,'bringup_h_'+name).argtypes = [C.c_void_p]
    lib.destbin_inspect.argtypes = [C.c_void_p,C.c_void_p]
    lib.destbin_prefix_matches.argtypes = [C.c_void_p,C.c_void_p,U32]
    lib.recover_check.argtypes = [SectorRead,U32,C.c_void_p]
    lib.recover_check.restype = C.c_int
    lib.bringup_h_recover_code.argtypes = [C.c_uint]
    lib.bringup_h_recover_code.restype = C.c_uint
    lib.fat_create.argtypes = [C.c_void_p,C.c_char_p,C.c_void_p,C.c_uint]
    lib.fat_mount.argtypes = [C.c_void_p,SectorRead,SectorWrite,U32]
    lib.fat_stream_open.argtypes = [C.c_void_p,C.c_char_p,C.c_void_p]
    lib.fat_stream_read.argtypes = [C.c_void_p,C.c_void_p,C.c_uint]
    return lib


@pytest.fixture
def h(native):
    native.bringup_h_init()
    return native


@pytest.fixture(scope='module')
def codes(native):
    """Result codes read from the real enum, never restated here."""
    names=['RECOVER_OK','RECOVER_NO_CARD','RECOVER_NO_FILESYSTEM','RECOVER_NO_CANDIDATE',
           'RECOVER_CANDIDATE_IO','RECOVER_CANDIDATE_INVALID','RECOVER_PREFIX_UNAVAILABLE']
    return {n:native.bringup_h_recover_code(i) for i,n in enumerate(names)}


def setup(h, kind, request, value=0, index=0, size=0, ack=False):
    s = struct.pack('<BBHHH',kind,request,value,index,size)
    out = (U8*64)()
    n = h.bringup_h_setup(s,out)
    if ack and n>=0:
        h.bringup_h_ack()
    return n,bytes(out)[:max(0,n)]


def configure(h):
    assert setup(h,0,5,7,ack=True)[0]==0
    assert setup(h,0,9,1,ack=True)[0]==0


def packet(s):
    b = s.encode() if isinstance(s,str) else s
    return b'$'+b+b'#'+f'{sum(b)&255:02x}'.encode()


def take(h, ax=False):
    out = (U8*8192)()
    if not ax:
        n=h.bringup_h_take(out)
        return bytes(out[:n])
    result = b''
    for _ in range(100):
        n=h.bringup_h_ax_take(out)
        if not n:
            return result
        result += bytes(out[:n])
    raise AssertionError('USB never drained')


def query(h, request, ax=False):
    p=packet(request)
    feed=h.bringup_h_ax_feed if ax else h.bringup_h_feed
    for at in range(0,len(p),64):
        feed(p[at:at+64],min(64,len(p)-at))
    return take(h,ax)


def test_descriptors_and_truncation(h):
    n,dev=setup(h,0x80,6,0x100,size=64)
    assert n==18 and dev[7]==64 and dev[8:12]==b'\x34\x12\x78\x56'
    n,cfg=setup(h,0x80,6,0x200,size=64)
    assert n==32 and struct.unpack_from('<H',cfg,2)[0]==32
    assert cfg[14:17]==b'\xff\x4c\x01'
    assert cfg[20]==0x81 and cfg[27]==1
    assert setup(h,0x80,6,0x200,size=9)[1]==cfg[:9]
    assert setup(h,0x80,6,0x300,size=64)[1]==b'\x04\x03\x09\x04'
    assert setup(h,0x80,6,0x301,index=0x409,size=64)[1][2:].decode('utf-16-le')=='Luma KC02 bring-up'
    assert setup(h,0x80,6,0x600,size=64)[0]==-1 # full speed: no qualifier


def test_address_configuration_ack_and_aborted_setup(h):
    setup(h,0,5,7)
    assert h.bringup_h_state(0)==0
    h.bringup_h_ack()
    assert h.bringup_h_state(0)==7
    setup(h,0,9,1)
    assert not h.bringup_h_state(1)
    setup(h,0x80,6,0x100,size=18) # preempts pending configuration
    h.bringup_h_ack()
    assert not h.bringup_h_state(1)
    setup(h,0,9,1,ack=True)
    assert h.bringup_h_state(1)==1
    assert not h.bringup_h_state(2)


@pytest.mark.parametrize('control_request',[
    (0,9,1,0,0), # cannot configure at address zero
    (0,5,128,0,0), (0,5,1,1,0), (0,5,1,0,1),
    (0x40,1,1,0,0), (0x40,0x5f,0,0,0), # revoke requires configured state
    (0x80,6,0x101,0,18), (0x80,6,0x301,0,64),
    (0x81,10,0,0,1), (2,3,0,0x81,0),
])
def test_invalid_usb_requests_stall(h,control_request):
    assert setup(h,*control_request)[0]==-1
    assert not h.bringup_h_state(2)


def test_debug_authorization_and_reset_revoke(h):
    configure(h)
    assert query(h,'g')==b'+'+packet('E22')
    h.bringup_h_authorize()
    data=query(h,'g')
    assert data.startswith(b'+$') and len(data)==285
    assert b'00000000'==data[2:10]
    h.bringup_h_reset()
    assert not h.bringup_h_state(0) and not h.bringup_h_state(1) and not h.bringup_h_state(2)


def test_device_can_revoke_but_usb_cannot_authorize(h):
    configure(h); h.bringup_h_authorize()
    assert setup(h,0x40,0x5f)[0]==0
    assert h.bringup_h_state(2)
    h.bringup_h_ack()
    assert not h.bringup_h_state(2)
    assert h.bringup_h_state(1)==1 and h.bringup_h_state(0)==7
    for req in (0,1,0x5e,0x60,0xff):
        assert setup(h,0x40,req)[0]==-1
    assert query(h,'g')==b'+'+packet('E22')


def test_monitor_memory_policy(h):
    configure(h); h.bringup_h_authorize()
    assert query(h,'m2000000,4')==b'+'+packet('00010203')
    assert query(h,'M27b0000,4:deadbeef')==b'+'+packet('OK')
    assert query(h,'m27b0000,4')==b'+'+packet('deadbeef')
    reads,writes=h.bringup_h_state(5),h.bringup_h_state(6)
    for address,size in ((0x8004,4),(0x27c0000,4),(0x27b0fff,2),(0xffffffff,4),(0,4)):
        assert query(h,f'm{address:x},{size:x}')==b'+'+packet('E01' if address==0xffffffff else 'E02')
    for address in (0x2000000,0x27c0000,0x8004,0x1ffffff):
        assert query(h,f'M{address:x},1:ff')==b'+'+packet('E02')
    assert (h.bringup_h_state(5),h.bringup_h_state(6))==(reads,writes)


def test_monitor_register_resume_limits(h):
    configure(h); h.bringup_h_authorize()
    for reg in (0,1,9,32,33,34):
        assert query(h,f'P{reg:x}=00000000')==b'+'+packet('E02')
    assert query(h,'P2=78563412')==b'+'+packet('OK')
    assert query(h,'p2')==b'+'+packet('78563412')
    for request in ('s','c2000000','Z0,2002000,4'):
        assert query(h,request)==b'+'+packet('' if request.startswith('Z') else 'E02')
    assert not h.bringup_h_state(3)
    assert query(h,'c2002010')==b'+'
    assert h.bringup_h_state(3)


def test_detach_ack_and_auth_revocation(h):
    configure(h); h.bringup_h_authorize()
    assert query(h,'D')==b'+'+packet('OK')
    assert h.bringup_h_state(3) and not h.bringup_h_state(2)


def test_bounded_queue_and_wrap(h):
    configure(h)
    for _ in range(100):
        assert h.bringup_h_queue(b'x'*100,100)
        assert take(h)==b'x'*100
    assert h.bringup_h_queue(b'x'*4096,4096)
    assert not h.bringup_h_queue(b'z',1)
    assert h.bringup_h_state(4) and take(h)==b''


def ax_configure(h):
    for s in (struct.pack('<BBHHH',0,5,7,0,0),struct.pack('<BBHHH',0,9,1,0,0)):
        h.bringup_h_ax_setup(s)
        h.bringup_h_ax_ack()
    assert h.bringup_h_state(9)==7 and h.bringup_h_state(1)==1


def test_controller_address_waits_for_status_ack(h):
    h.bringup_h_ax_setup(struct.pack('<BBHHH',0,5,7,0,0))
    assert h.bringup_h_state(9)==0
    h.bringup_h_ax_ack()
    assert h.bringup_h_state(9)==7


def test_controller_aborted_setup_does_not_apply_address(h):
    h.bringup_h_ax_setup(struct.pack('<BBHHH',0,5,7,0,0))
    h.bringup_h_ax_setup_end()
    h.bringup_h_ax_ack()
    assert h.bringup_h_state(0)==0 and h.bringup_h_state(9)==0


def test_controller_bulk_rsp_and_suspend(h):
    ax_configure(h); h.bringup_h_authorize()
    assert query(h,'g',ax=True).startswith(b'+$00000000')
    assert len(query(h,'g',ax=True))==285
    h.bringup_h_ax_event(1) # suspend is not bus reset
    assert h.bringup_h_state(0)==7 and h.bringup_h_state(1)==1
    assert not h.bringup_h_state(2)
    assert query(h,'g',ax=True)==b'+'+packet('E22')
    h.bringup_h_ax_event(4)
    assert h.bringup_h_state(0)==0 and h.bringup_h_state(1)==0


def test_controller_oversize_packet_fails_closed(h):
    ax_configure(h); h.bringup_h_authorize()
    h.bringup_h_ax_feed(b'x'*64,65)
    assert h.bringup_h_state(4) and not h.bringup_h_state(2)


ByteRead=C.CFUNCTYPE(C.c_int,C.c_void_p,U32,C.c_void_p,C.c_uint)
SectorRead=C.CFUNCTYPE(C.c_int,U32,C.c_void_p)
SectorWrite=C.CFUNCTYPE(C.c_int,U32,C.c_void_p)
class Source(C.Structure):
    _fields_=[('context',C.c_void_p),('read',ByteRead),('size',U32)]
class Info(C.Structure):
    _fields_=[(n,U32) for n in ('size','crc','app_offset','app_size')]


def source(data, fail=None):
    @ByteRead
    def read(ctx,at,out,n):
        if at+n>len(data) or (fail is not None and at<=fail<at+n):
            return 0
        C.memmove(out,bytes(data[at:at+n]),n)
        return 1
    return Source(None,read,len(data))


def legacy():
    b=bytearray(b'\xff'*0x400000)
    b[4:8]=b'BLDR'; b[510:512]=b'\x55\xaa'; b[0x304:0x308]=b'SFAT'
    struct.pack_into('<6I',b,0x20,0x02000000,0x13,0x644,0,0x01234567,0x01234567)
    b[0x0c]=(b[0x0c]-sum(b[:512]))&255
    return b


def test_legacy_full_stream_and_protected_prefix(h):
    b=legacy(); a=source(b)
    info=Info()
    assert h.destbin_inspect(C.byref(a),C.byref(info))==0
    assert info.crc==zlib.crc32(b) and info.size==0x400000 and info.app_size==0x644*512
    # Prefix comparison is a separate decision: a caller that cannot obtain a
    # trusted running snapshot must not be able to call the candidate verified.
    assert h.destbin_prefix_matches(C.byref(a),C.byref(source(b)),0x20000)==0
    b[0x10000]^=1
    assert h.destbin_prefix_matches(C.byref(source(b)),C.byref(source(legacy())),0x20000)==3
    # A change outside the protected span is not a prefix failure.
    b=legacy(); b[0x30000]^=1
    assert h.destbin_prefix_matches(C.byref(source(b)),C.byref(source(legacy())),0x20000)==0
    for bad_bytes in (0,0x25ff,0x400001):
        assert h.destbin_prefix_matches(C.byref(a),C.byref(source(legacy())),bad_bytes)==1


@pytest.mark.parametrize('offset',[4,0x20,0x24,0x28,0x30,0x34,0x1fe,0x304])
def test_legacy_rejects_malformed(h,offset):
    b=legacy(); b[offset]^=1
    if offset<512:
        b[0x0c]=(b[0x0c]-sum(b[:512]))&255
    a=source(b); info=Info()
    assert h.destbin_inspect(C.byref(a),C.byref(info))==1
    assert tuple(getattr(info,n) for n,_ in info._fields_)==(0,0,0,0)


def test_legacy_io_and_size_failure(h):
    b=legacy(); info=Info()
    assert h.destbin_inspect(C.byref(source(b,0x300000)),C.byref(info))==2
    assert h.destbin_inspect(C.byref(source(b[:-1])),C.byref(info))==1
    assert h.destbin_inspect(C.byref(source(b[:0])),C.byref(info))==1


def stock_or_skip():
    p=ROOT/'firmware/original.bin'
    if not p.exists():
        pytest.skip('private pinned stock dump unavailable')
    return p.read_bytes()


def manifest(ram, **extra):
    return dict(board='original',kind='UNQUALIFIED BRINGUP RAM',selftest=False,
                entry=0x200046c,load=0x2000000,sha256=hashlib.sha256(ram).hexdigest(),size=len(ram),**extra)


def test_factory_preserves_boot_resources_and_header():
    stock=stock_or_skip(); ram=b'\x00'*0x46c+b'\x01\x02\x03\x04'
    result,report=factory.candidate(stock,ram,manifest(ram))
    assert result[:0x2600]==stock[:0x2600]
    end=0x2600+0x644*512
    assert result[end:]==stock[end:]
    assert result[0x2600:0x2600+len(ram)]==ram
    assert report['factory_copy_end']==0xcae00 and not report['flash_qualified']
    for bad in (dict(manifest(ram),selftest=True),dict(manifest(ram),board='z12'),
                dict(manifest(ram),entry=0x2000000),dict(manifest(ram),sha256='0'*64)):
        with pytest.raises(ValueError):
            factory.candidate(stock,ram,bad)
    with pytest.raises(ValueError):
        factory.candidate(stock[:-1],ram,manifest(ram))


@pytest.mark.parametrize('name',['DestBin.bin','DESTBIN.BIN','DestBin.img','firmware.img'])
def test_factory_refuses_install_names(tmp_path,name):
    with pytest.raises(ValueError,match='auto-update filename'):
        factory.create('unused','unused','unused',tmp_path/name)


@pytest.mark.parametrize('pair',[(0x219,0x3280),(0x1908,0x3282),(0x1908,0x3283),(0,1),(1,0),(65536,1)])
def test_bridge_refuses_stock_and_invalid_identities(pair):
    with pytest.raises(ValueError):
        bridge.check_identity(*pair)


def test_bridge_rejects_partial_usb_write():
    class Device:
        def write(self,*args,**kwargs): return 1
    with pytest.raises(OSError,match='partial USB OUT'):
        bridge.write_all(Device(),b'123',100)


card=module('luma_card', FW/'tools/card.py')
sdcreate=module('kc02_create_sd', ROOT/'tools/emu/create_sd.py')

_counter=[0]


def card_with(tmp_path, entries):
    """A private, create-only FAT16 fixture; never an existing user card."""
    _counter[0]+=1
    image=tmp_path/f'card-{_counter[0]}.img'
    sdcreate.create_image(image)
    if entries:
        card.seed(image, entries)
    return image


def card_reader(image):
    data=image.read_bytes()

    @SectorRead
    def read(sector,out):
        if sector+1>len(data)//512:
            return 0
        C.memmove(out,data[sector*512:sector*512+512],512)
        return 1
    return read


class _Info(C.Structure):
    _fields_=[(n,U32) for n in ('size','crc','app_offset','app_size')]


class _Report(C.Structure):
    _fields_=[('result',C.c_int),('info',_Info),('capacity',U32)]


def recover(h,image):
    report=_Report()
    result=h.recover_check(card_reader(image),image.stat().st_size//512*512,C.byref(report))
    return result,report


def test_recover_result_codes_match_the_header(h,codes):
    assert codes['RECOVER_OK']==0
    assert [codes[n] for n in ('RECOVER_NO_CARD','RECOVER_NO_FILESYSTEM','RECOVER_NO_CANDIDATE',
                               'RECOVER_CANDIDATE_IO','RECOVER_CANDIDATE_INVALID',
                               'RECOVER_PREFIX_UNAVAILABLE')]==[1,2,3,4,5,6]
    assert h.bringup_h_recover_code(99)==0xffffffff


def test_recover_no_card_and_unreadable_card(h,tmp_path,codes):
    @SectorRead
    def dead(sector,out): return 0
    report=_Report()
    assert h.recover_check(dead,1<<20,C.byref(report))==codes['RECOVER_NO_FILESYSTEM']
    assert h.recover_check(dead,0,C.byref(report))==codes['RECOVER_NO_CARD']
    assert h.recover_check(C.cast(None,SectorRead),1<<20,C.byref(report))==codes['RECOVER_NO_CARD']


def test_recover_valid_candidate_is_not_reported_verified(h,tmp_path,codes):
    candidate=legacy()
    image=card_with(tmp_path,[('DESTBIN.BIN',bytes(candidate))])
    result,report=recover(h,image)
    # Structure and CRC pass, but the boot prefix cannot be verified yet, so the
    # gate must not report the candidate as verified or installable.
    assert result==codes['RECOVER_PREFIX_UNAVAILABLE'], result
    assert result!=codes['RECOVER_OK']
    assert report.capacity==image.stat().st_size
    assert report.info.crc==zlib.crc32(candidate)
    assert report.info.app_size==0x644*512 and report.info.app_offset==0x2600


def test_recover_rejects_absent_malformed_and_truncated(h,tmp_path,codes):
    invalid=codes['RECOVER_CANDIDATE_INVALID']
    assert recover(h,card_with(tmp_path,[]))[0]==codes['RECOVER_NO_CANDIDATE']
    assert recover(h,card_with(tmp_path,[('DESTBIN.BIN',legacy()[:0x300000])]))[0]==invalid
    assert recover(h,card_with(tmp_path,[('DESTBIN.BIN',b'x')]))[0]==invalid
    for offset in (4,0x20,0x24,0x28,0x30,0x34,0x1fe,0x304):
        broken=bytearray(legacy())
        broken[offset]^=1
        if offset<512:
            broken[0x0c]=(broken[0x0c]-sum(broken[:512]))&255
        assert recover(h,card_with(tmp_path,[('DESTBIN.BIN',bytes(broken))]))[0]==invalid, hex(offset)


def test_recover_fingerprint_detects_a_modified_image(h,tmp_path,codes):
    # DestBin stores no image CRC, so a bit flip outside the boot/recovery
    # prefix is structurally valid. The gate therefore reports a fingerprint
    # for the operator to compare, and that is the only defence for those
    # bytes. It must never be described as authentication.
    good=legacy()
    modified=bytearray(good); modified[0x90000]^=1
    ok_image=card_with(tmp_path,[('DESTBIN.BIN',good)])
    modified_image=card_with(tmp_path,[('DESTBIN.BIN',bytes(modified))])
    first,report=recover(h,ok_image)
    second,other=recover(h,modified_image)
    assert first==second==codes['RECOVER_PREFIX_UNAVAILABLE']
    assert report.info.crc==zlib.crc32(good)
    assert other.info.crc==zlib.crc32(bytes(modified))
    assert report.info.crc!=other.info.crc
    # Inside the protected prefix the comparison does catch it,
    # but only once a trusted running snapshot is available.
def test_recover_reports_io_failure_mid_stream(h,tmp_path,codes):
    image=card_with(tmp_path,[('DESTBIN.BIN',bytes(legacy()))])
    data=image.read_bytes()
    calls=[0]

    @SectorRead
    def read(sector,out):
        calls[0]+=1
        if calls[0]>900: return 0
        C.memmove(out,data[sector*512:sector*512+512],512)
        return 1
    report=_Report()
    assert h.recover_check(read,len(data),C.byref(report))==codes['RECOVER_CANDIDATE_IO']
    assert calls[0]>900


def test_recover_read_only_mount_refuses_creation(h,tmp_path,codes):
    # The property recover_check depends on: a mount with no write callback can
    # never publish a file, so the gate cannot modify the card it inspects.
    image=card_with(tmp_path,[('DESTBIN.BIN',b'x')])
    fat=C.create_string_buffer(4096)
    assert h.fat_mount(fat,card_reader(image),C.cast(None,SectorWrite),image.stat().st_size)
    assert h.fat_create(fat,b'NEW.BIN',b'y',1)==0
    assert recover(h,image)[0]==codes['RECOVER_CANDIDATE_INVALID']  # 1 byte is not 4 MiB


def test_stream_reader_streams_without_whole_file_buffer(h,tmp_path):
    payload=bytes(range(256))*300
    image=card_with(tmp_path,[('BIG.BIN',payload)])
    read=card_reader(image)
    data=image.read_bytes()
    fat=C.create_string_buffer(4096)
    assert h.fat_mount(fat,read,C.cast(None,SectorWrite),len(data))
    stream=C.create_string_buffer(4096)
    assert h.fat_stream_open(fat,b'BIG.BIN',stream)
    out=(U8*512)()
    total=b''
    while True:
        got=h.fat_stream_read(stream,out,512)
        if got<=0:
            break
        total+=bytes(out[:got])
    assert got==0 and total==payload
    assert h.fat_stream_read(stream,out,512)==0


def test_stream_reader_rejects_short_chain_and_bad_names(h,tmp_path):
    image=card_with(tmp_path,[('BIG.BIN',b'z'*70000)])
    read=card_reader(image)
    data=image.read_bytes()
    fat=C.create_string_buffer(4096)
    assert h.fat_mount(fat,read,C.cast(None,SectorWrite),len(data))
    for name in (b'big.bin',b'MISSING.BIN',b'../BIG.BIN',b'TOOLONGNAME.BIN'):
        assert h.fat_stream_open(fat,name,C.create_string_buffer(4096))==0
