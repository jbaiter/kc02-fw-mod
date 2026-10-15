"""Recovery/debug bring-up: offline layout, USB contracts, monitor access policy.

The register model here is not physical USB validation. QEMU tests live in the
opt-in bringup_smoke.py runner because the USB controller is not emulated there.
"""
import ctypes as C
import hashlib
import importlib.util
import json
from pathlib import Path
import random
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
                    *(str(FW/'src'/s) for s in ('debug.c','monitor.c','usb_device.c','ax_usb.c','destbin.c',
                                                'storage.c','recover.c','or1k_decode.c',
                                                'msc.c','msc_medium.c','update.c','update_sd.c','crc.c')),
                    str(FW/'tests/bringup_harness.c'),'-o',str(libpath)],check=True)
    lib = C.CDLL(str(libpath))
    lib.bringup_h_setup.argtypes = [C.c_void_p,C.c_void_p]
    lib.bringup_h_state.argtypes = [C.c_uint]
    for name in ('state','reg','get_code','code_syncs','stop_reason','reads','writes','patched'):
        getattr(lib,'bringup_h_'+name).restype = C.c_uint
    lib.bringup_h_reg.argtypes = [C.c_uint]
    lib.bringup_h_wire.argtypes = [C.c_void_p,C.c_uint]
    lib.bringup_h_set_code.argtypes = [U32,U32]
    lib.bringup_h_msc_command.argtypes = []
    lib.bringup_h_msc_command.restype = C.c_int
    for name in ('msc_export','msc_reclaim'):
        getattr(lib,'bringup_h_'+name).argtypes = [C.c_int]
    for name in ('msc_info','card_byte'):
        getattr(lib,'bringup_h_'+name).argtypes = [C.c_uint]
        getattr(lib,'bringup_h_'+name).restype = C.c_uint
    lib.bringup_h_msc_sense.argtypes = []
    lib.bringup_h_msc_sense.restype = C.c_uint
    lib.bringup_h_ax_msc_take.argtypes = [C.c_void_p,C.c_uint]
    lib.bringup_h_ax_msc_take.restype = C.c_uint
    lib.bringup_h_ax_msc_feed.argtypes = [C.c_void_p,C.c_uint]
    lib.bringup_h_get_code.argtypes = [U32]
    lib.bringup_h_script.argtypes = [C.c_uint,C.c_char_p]
    lib.bringup_h_checkpoint.argtypes = [C.c_void_p]
    lib.bringup_h_trap.argtypes = [C.c_void_p,U32,U32]
    for name in ('feed','queue','ax_feed'):
        getattr(lib,'bringup_h_'+name).argtypes = [C.c_void_p,C.c_uint]
    for name in ('take','ax_take','ax_setup'):
        getattr(lib,'bringup_h_'+name).argtypes = [C.c_void_p]
    lib.destbin_inspect.argtypes = [C.c_void_p,C.c_void_p]
    lib.destbin_prefix_matches.argtypes = [C.c_void_p,C.c_void_p,U32]
    lib.recover_check.argtypes = [C.c_void_p,SectorRead,U32,C.c_void_p]
    lib.recover_check.restype = C.c_int
    lib.bringup_h_recover_code.argtypes = [C.c_uint]
    lib.bringup_h_recover_code.restype = C.c_uint
    lib.fat_create.argtypes = [C.c_void_p,C.c_char_p,C.c_void_p,C.c_uint]
    lib.fat_mount.argtypes = [C.c_void_p,SectorRead,SectorWrite,U32]
    lib.fat_stream_open.argtypes = [C.c_void_p,C.c_char_p,C.c_void_p]
    lib.fat_stream_read.argtypes = [C.c_void_p,C.c_void_p,C.c_uint]
    lib.or1k_successors.argtypes = [U32,U32,C.c_void_p,U32,C.c_void_p]
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


def mem_hex(word):
    """The bytes a little-endian word occupies in memory, in address order."""
    return struct.pack('<I', word).hex()


def drain(h, ax=False):
    """Discards anything already queued, such as an earlier stop report."""
    out=(U8*8192)()
    if ax:
        for _ in range(100):
            if not h.bringup_h_ax_take(out):
                return
    else:
        h.bringup_h_take(out)


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


def le_hex(word):
    """Register values travel as little-endian bytes.

    The RSP `p`/`P` payload is the target's byte order, which is what GDB
    expects from a little-endian target. `m`/`M` instead carry memory bytes in
    address order. Both are self-consistent; mixing them up looks exactly like
    a broken target.
    """
    return struct.pack('<I', word).hex()


def query(h, request, ax=False):
    drain(h, ax)
    p=packet(request)
    feed=h.bringup_h_ax_feed if ax else h.bringup_h_feed
    for at in range(0,len(p),64):
        feed(p[at:at+64],min(64,len(p)-at))
    return take(h,ax)


def test_descriptors_and_truncation(h):
    n,dev=setup(h,0x80,6,0x100,size=64)
    assert n==18 and dev[7]==64 and dev[8:12]==b'\x34\x12\x78\x56'
    # A composite device: mass storage on interface 0 with EP1, vendor debug on
    # interface 1 with EP2. A browser may claim the vendor interface; it can
    # never be given class 08, which is why they are separate functions.
    n,cfg=setup(h,0x80,6,0x200,size=64)
    assert n==55 and struct.unpack_from('<H',cfg,2)[0]==55
    # Layout: config(9) iface0(9) ep(7) ep(7) iface1(9) ep(7) ep(7) = 55.
    assert cfg[4]==2                                    # two interfaces
    assert cfg[9]==9 and cfg[18]==7 and cfg[25]==7      # first function sizes
    assert cfg[32]==9 and cfg[41]==7 and cfg[48]==7     # second function sizes
    assert cfg[11]==0 and cfg[14:17]==b'\x08\x06\x50' # interface 0: mass storage
    assert cfg[13]==2 and cfg[20]==0x81 and cfg[27]==0x01
    assert cfg[34]==1 and cfg[37:40]==b'\xff\x4c\x01' # interface 1: vendor debug
    assert cfg[36]==2 and cfg[43]==0x82 and cfg[50]==0x02
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


USB_DEBUG_INTERFACE = 1
USB_MSC_INTERFACE = 0


def test_device_can_revoke_but_usb_cannot_authorize(h):
    configure(h); h.bringup_h_authorize()
    # The revoke message belongs to the debug interface and cannot reach the
    # storage function.
    assert setup(h,0x40,0x5f,index=0)[0]==-1
    assert setup(h,0x40,0x5f,index=USB_DEBUG_INTERFACE)[0]==0
    assert h.bringup_h_state(2)
    h.bringup_h_ack()
    assert not h.bringup_h_state(2)
    assert h.bringup_h_state(1)==1 and h.bringup_h_state(0)==7
    for req in (0,1,0x5e,0x60,0xff):
        assert setup(h,0x40,req,index=USB_DEBUG_INTERFACE)[0]==-1
    assert query(h,'g')==b'+'+packet('E22')


MON_STOP_CHECKPOINT, MON_STOP_BREAKPOINT, MON_STOP_STEP, MON_STOP_EXCEPTION = 0,1,2,3


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


TRAP = 0x21000000
ADDR = 0x02002000


def script(h, *bodies):
    h.bringup_h_script_clear()
    for i, body in enumerate(bodies):
        h.bringup_h_script(i, body.encode())


def wire(h):
    buf = (U8*256)()
    n = h.bringup_h_wire(buf, 256)
    return bytes(buf[:n])


def test_monitor_register_resume_limits(h):
    configure(h); h.bringup_h_authorize()
    for reg in (0,1,9,32,34):
        assert query(h,f'P{reg:x}=00000000')==b'+'+packet('E02')
    assert query(h,'P2=78563412')==b'+'+packet('OK')
    assert query(h,'p2')==b'+'+packet('78563412')
    # npc is only writable for a trap stop, so a checkpoint stop refuses it.
    assert query(h,f'P21={le_hex(0x02002000)}')==b'+'+packet('E02')


def test_npc_writable_for_trap_but_only_into_code(h):
    configure(h); h.bringup_h_authorize()
    # An empty script releases the target with no RSP traffic, so the session
    # stays authorised and the stop state can be inspected.
    script(h)
    h.bringup_h_trap(None, ADDR, 0x8001)
    assert query(h,f'P21={le_hex(0x02003000)}')==b'+'+packet('OK')
    assert h.bringup_h_reg(33)==0x02003000
    assert query(h,f'P21={le_hex(0x00008004)}')==b'+'+packet('E02')       # outside the code region
    assert query(h,f'P21={le_hex(0x027b0000)}')==b'+'+packet('E02')    # scratch is not code


def test_breakpoint_install_hit_step_and_rearm(h):
    configure(h); h.bringup_h_authorize()
    h.bringup_h_set_code(ADDR, 0x9c800000)     # l.addi r4,r0,0
    h.bringup_h_set_code(ADDR+4, 0x9c600004)   # l.addi r3,r0,4: one successor
    before = h.bringup_h_code_syncs()
    script(h, f'Z0,{ADDR:x},4', 'c')
    h.bringup_h_checkpoint(None)
    assert h.bringup_h_get_code(ADDR) == TRAP
    assert h.bringup_h_code_syncs() > before, 'patching code must ask for a sync'
    # Stopped at the trap: the monitor must classify it as the breakpoint.
    script(h, 's')
    h.bringup_h_trap(None, ADDR, 0x8001)
    assert h.bringup_h_stop_reason() == MON_STOP_BREAKPOINT
    # The step lifted the breakpoint and put a temporary trap on the successor.
    assert h.bringup_h_get_code(ADDR) == 0x9c800000
    assert h.bringup_h_get_code(ADDR+4) == TRAP
    # Landing on the successor completes the step and re-arms the breakpoint.
    script(h, 's')
    h.bringup_h_trap(None, ADDR+4, 0x8001)
    assert h.bringup_h_stop_reason() == MON_STOP_STEP
    assert h.bringup_h_get_code(ADDR) == TRAP, 'the breakpoint must be re-armed'
    assert h.bringup_h_get_code(ADDR+8) == TRAP, 'the next step is armed'


def test_breakpoint_remove_and_detach_restore_everything(h):
    configure(h); h.bringup_h_authorize()
    h.bringup_h_set_code(ADDR, 0x9c800000)
    h.bringup_h_set_code(ADDR+4, 0x9c600004)   # l.addi r3,r0,4
    # Only requests that arrive before the target is released run inside the
    # stop loop; the rest are issued once it is running again.
    script(h, f'Z0,{ADDR:x},4', f'Z0,{ADDR+4:x},4')
    h.bringup_h_checkpoint(None)
    assert h.bringup_h_get_code(ADDR) == TRAP
    assert h.bringup_h_get_code(ADDR+4) == TRAP
    assert query(h, f'z0,{ADDR+4:x},4') == b'+'+packet('OK')
    assert h.bringup_h_get_code(ADDR+4) == 0x9c600004
    # m carries memory bytes in address order, so the word appears reversed.
    assert query(h, f'm{ADDR+4:x},4') == b'+'+packet(mem_hex(0x9c600004))
    assert h.bringup_h_get_code(ADDR) == TRAP
    assert h.bringup_h_patched()
    # Detaching from a trap stop lifts the remaining breakpoint.
    script(h, 'D')
    h.bringup_h_trap(None, ADDR, 0x8001)
    assert h.bringup_h_stop_reason() == MON_STOP_BREAKPOINT
    assert not h.bringup_h_patched()
    assert h.bringup_h_get_code(ADDR) == 0x9c800000


def test_breakpoint_rejects_non_code_and_full_table(h):
    configure(h); h.bringup_h_authorize()
    for address in (0x8004, 0x027b0000, 0x020c8800, 0x10000000):
        assert query(h,f'Z0,{address:x},4')==b'+'+packet('')
    for i in range(8):
        assert query(h,f'Z0,{ADDR+i*4:x},4')==b'+'+packet('OK')
    assert query(h,f'Z0,{ADDR+32:x},4')==b'+'+packet('')
    # Only software breakpoints of the width this target can patch.
    assert query(h,f'Z1,{ADDR:x},4')==b'+'+packet('')
    assert query(h,f'Z0,{ADDR:x},2')==b'+'+packet('')


@pytest.mark.parametrize('word,armed',[(0x24000000,True),(0x20000000,False),(0x21000000,False)])
def test_step_refuses_unknown_and_stopping_instructions(h,word,armed):
    configure(h); h.bringup_h_authorize()
    h.bringup_h_set_code(ADDR, word)
    if armed:
        script(h, f'Z0,{ADDR:x},4', 'c')
    else:
        script(h, 'c')
    h.bringup_h_checkpoint(None)
    script(h)
    h.bringup_h_trap(None, ADDR, 0x8001)
    assert h.bringup_h_reg(33) == ADDR
    assert h.bringup_h_get_code(ADDR) == (TRAP if armed else word)
    # l.rfe has no successor this decoder will guess, and l.trap/l.sys stop.
    # A refused step is an explicit error, not silence, so the client can say why.
    assert query(h,'s')==b'+'+packet('E02')
    assert h.bringup_h_get_code(ADDR) == (TRAP if armed else word)


def test_step_refuses_a_branch_to_its_own_address(h):
    configure(h); h.bringup_h_authorize()
    h.bringup_h_set_code(ADDR, 0x0c000000)   # l.bnf to its own address
    script(h, f'Z0,{ADDR:x},4', 'c')
    h.bringup_h_checkpoint(None)
    assert query(h,'s')==b'+'+packet('E02')
    # The breakpoint must still be armed: a refused step changes nothing.
    assert h.bringup_h_get_code(ADDR) == TRAP


def test_step_arms_both_successors_of_a_conditional_branch(h):
    configure(h); h.bringup_h_authorize()
    # l.bf with a forward offset, so taken and fall-through differ.
    h.bringup_h_set_code(ADDR, 0x10000002)
    script(h, f'Z0,{ADDR:x},4', 'c')
    h.bringup_h_checkpoint(None)
    script(h, 's')
    h.bringup_h_trap(None, ADDR, 0x8001)
    assert h.bringup_h_stop_reason() == MON_STOP_BREAKPOINT
    assert h.bringup_h_get_code(ADDR+4) == TRAP
    assert h.bringup_h_get_code(ADDR+8) == TRAP


def test_continue_from_a_breakpoint_steps_over_it_without_reporting(h):
    configure(h); h.bringup_h_authorize()
    h.bringup_h_set_code(ADDR, 0x9c800000)
    script(h, f'Z0,{ADDR:x},4', 'c')
    h.bringup_h_checkpoint(None)
    # Continuing from the breakpoint lifts it and hides the intermediate stop.
    script(h, 'c')
    h.bringup_h_trap(None, ADDR, 0x8001)
    assert h.bringup_h_get_code(ADDR) == 0x9c800000
    assert h.bringup_h_get_code(ADDR+4) == TRAP
    # The successor stop is swallowed and re-arms the breakpoint, so the target
    # keeps running until the next real event.
    script(h, 's')
    h.bringup_h_trap(None, ADDR+4, 0x8001)
    assert h.bringup_h_get_code(ADDR) == TRAP


def test_unexpected_trap_reports_bus_error(h):
    configure(h); h.bringup_h_authorize()
    script(h)
    h.bringup_h_trap(None, ADDR, 0x8001)
    assert h.bringup_h_stop_reason() == MON_STOP_EXCEPTION


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


class Successors(C.Structure):
    _fields_=[('count',C.c_uint),('address',U32*2),('stops',C.c_int),('unknown',C.c_int)]


def successors(h,pc,word,gpr=None):
    regs=(U32*32)()
    if gpr:
        for index,value in gpr.items():
            regs[index]=value
    out=Successors()
    ok=h.or1k_successors(pc,word,regs,0x02000000,C.byref(out))
    return ok,[out.address[i] for i in range(out.count)],out.stops,out.unknown


# Encodings taken from the toolchain's own disassembly, so the decoder is pinned
# to real instructions rather than to my reading of the ISA.
PC = 0x02002000


@pytest.mark.parametrize('word,targets,stops,unknown',[
    (0x15000000,[PC+4],0,0),        # l.nop
    (0x9c600004,[PC+4],0,0),        # l.addi r3,r0,4
    (0x03fffffa,[PC-24],0,0),       # l.j, target 24 bytes back
    (0x07fffff9,[PC-28],0,0),       # l.jal
    (0x13fffff8,[PC-32,PC+4],0,0),  # l.bf: the taken target is listed first
    (0x0ffffff7,[PC+4,PC-36],0,0),  # l.bnf: the fall-through is listed first
    (0x21000000,[],1,0),            # l.trap
    (0x20000000,[],1,0),            # l.sys
    (0x24000000,[],0,1),            # l.rfe
])
def test_decoder_successors(h,word,targets,stops,unknown):
    ok,target,got_stops,got_unknown=successors(h,PC,word)
    assert bool(ok)==bool(not unknown)
    assert got_stops==stops and got_unknown==unknown
    if not stops:
        assert target==targets


def test_decoder_register_jumps(h):
    for word,target in ((0x44004800,0xdeadbeef),(0x48004800,0xdeadbeef)):
        ok,targets,stops,unknown=successors(h,PC,word,{9:0xdeadbeef})
        assert ok and not stops and not unknown and targets==[0xdeadbeef]


def test_decoder_is_bounded_on_arbitrary_words(h):
    rng=random.Random(3292)
    for _ in range(2000):
        out=Successors()
        word=rng.getrandbits(32)
        h.or1k_successors(PC,word,(U32*32)(),0, C.byref(out))
        assert out.count<=2, hex(word)


def test_decoder_identifies_only_trap_words(h):
    assert not h.or1k_is_trap(0x20000000)
    assert not h.or1k_is_trap(0x9c600004)
    assert h.or1k_is_trap(0x21000000)


SENSE_NOT_READY, SENSE_ILLEGAL_REQUEST, SENSE_UNIT_ATTENTION = 0x02, 0x05, 0x06
MSC_EXPORTS, MSC_RECLAIMS, MSC_REFUSED, MSC_HOST_OWNS = 0, 1, 2, 3
MSC_FAT_OWNER, MSC_FAT_MOUNTED, MSC_MEDIUM_BLOCKS, MSC_COMMANDS, MSC_ERRORS, MSC_STALLS = 4, 5, 6, 7, 8, 9
CARD_BLOCKS = 16


def msc_setup(h):
    """Configures the composite device and binds the engine to the endpoints."""
    configure(h)
    h.bringup_h_msc_init()
    assert h.bringup_h_state(1) == 1


def cdb6(opcode, *fields):
    """A six-byte CDB: opcode, then the command's own fields, then padding."""
    assert len(fields) <= 5
    return bytes([opcode]) + bytes(fields) + bytes(5-len(fields))


def cdb_read_capacity():
    return bytes([0x25]) + bytes(9)


def cdb_read10(lba, blocks, flags=0):
    """READ(10): opcode, flags, LBA, reserved, length, control."""
    return bytes([0x28, flags]) + struct.pack('>I', lba) + b'\x00' + \
           struct.pack('>H', blocks) + b'\x00'


def cdb_write10(lba, blocks, flags=0):
    return bytes([0x2a, flags]) + struct.pack('>I', lba) + b'\x00' + \
           struct.pack('>H', blocks) + b'\x00' 


def cbw(cdb, length, inbound=False):
    """A Bulk-Only CBW frame for the given command block."""
    frame = (U8*31)()
    body = struct.pack('<II', 0x43425355, 1) + struct.pack('<I', length) + \
           bytes([0x00 if inbound else 0x80, 0, len(cdb)]) + cdb
    for i, byte in enumerate(body):
        frame[i] = byte
    return frame


def msc_run(h, frame, data_in=b''):
    """Sends a CBW, then any OUT data, on the storage endpoint.

    The CBW comes first: that ordering is the protocol, not a convenience. A
    test that sends data first hands the engine a payload where it expects a
    command block.
    """
    h.bringup_h_ax_msc_feed(frame, 31)
    for at in range(0, len(data_in), 64):
        chunk = data_in[at:at+64]
        buf = (U8*len(chunk)).from_buffer_copy(chunk)
        h.bringup_h_ax_msc_feed(buf, len(chunk))
    status = h.bringup_h_msc_command()
    out = (U8*8192)()
    n = h.bringup_h_ax_msc_take(out, 8192)
    body = bytes(out[:n])
    return status, body


def csw_of(body):
    return body[-13:] if len(body) >= 13 else b''


def csw_status(body):
    return csw_of(body)[12] if len(csw_of(body)) == 13 else -1


def payload_of(body):
    """The data the device sent, with the trailing CSW removed."""
    return body[:-13] if len(body) >= 13 else b''


def test_mass_storage_is_not_ready_until_the_card_is_handed_over(h):
    """A card the camera still owns must read as no medium, not a blank disk."""
    msc_setup(h)
    status, body = msc_run(h, cbw(cdb6(0x00), 0))            # TEST UNIT READY
    assert status == 1 and csw_status(body) == 1
    assert struct.unpack_from('<I', csw_of(body), 0)[0] == 0x53425355
    assert h.bringup_h_msc_sense() >> 8 == SENSE_NOT_READY
    status, body = msc_run(h, cbw(cdb_read_capacity(), 8))
    assert status == 1
    assert h.bringup_h_msc_sense() >> 8 == SENSE_NOT_READY
    # A read must not transfer data for a medium that is not there.
    status, body = msc_run(h, cbw(cdb_read10(0, 1), 512))
    assert status == 1 and payload_of(body) == b''


def test_handed_over_card_is_a_working_disk(h):
    msc_setup(h)
    assert h.bringup_h_msc_export(1) == 1
    assert h.bringup_h_msc_info(MSC_HOST_OWNS) == 1
    assert h.bringup_h_msc_info(MSC_MEDIUM_BLOCKS) == CARD_BLOCKS

    assert msc_run(h, cbw(cdb6(0x00), 0))[0] == 0            # TEST UNIT READY

    status, body = msc_run(h, cbw(cdb_read_capacity(), 8))
    assert status == 0
    cap = payload_of(body)
    assert struct.unpack('>I', cap[0:4])[0] == CARD_BLOCKS-1
    assert struct.unpack('>I', cap[4:8])[0] == 512

    status, body = msc_run(h, cbw(cdb6(0x12, 0, 0, 0, 36), 36))   # INQUIRY
    assert status == 0
    inq = payload_of(body)
    assert inq[:5] == bytes([0x00, 0x00, 0x05, 0x02, 31])
    assert inq[8:16] == b'LUMA    ' and inq[16:32] == b'KC02 SD CARD    '

    status, body = msc_run(h, cbw(cdb_read10(2, 1), 512))
    assert status == 0 and len(payload_of(body)) == 512
    expected = bytes(h.bringup_h_card_byte(2*512+i) for i in range(512))
    assert payload_of(body) == expected

    written = bytes((i*11+3) & 0xff for i in range(512))
    status, body = msc_run(h, cbw(cdb_write10(5, 1), 512, inbound=True), data_in=written)
    assert status == 0 and csw_status(body) == 0
    assert bytes(h.bringup_h_card_byte(5*512+i) for i in range(512)) == written

    # A short read reports the unconsumed bytes as residue.
    status, body = msc_run(h, cbw(cdb_read10(0, 3), 512))
    assert status == 0 and len(payload_of(body)) == 512
    assert struct.unpack_from('<I', csw_of(body), 8)[0] == 1024


def test_get_max_lun_and_bulk_only_reset(h):
    msc_setup(h)
    n, out = setup(h, 0xa1, 0xfe, index=USB_MSC_INTERFACE, size=1)
    assert n == 1 and out == b'\x00'
    # The class request belongs to the storage interface, not the debug one.
    assert setup(h, 0xa1, 0xfe, index=USB_DEBUG_INTERFACE, size=1)[0] == -1
    assert setup(h, 0x21, 0xff, index=0, size=0)[0] == 0
    h.bringup_h_ack()          # the reset takes effect when it completes
    h.bringup_h_msc_export(1)
    # The next command must report that a reset happened, once.
    assert msc_run(h, cbw(cdb6(0x00), 0))[0] == 1
    assert h.bringup_h_msc_sense() >> 8 == SENSE_UNIT_ATTENTION
    assert msc_run(h, cbw(cdb6(0x00), 0))[0] == 0


def test_taking_the_card_back_invalidates_the_local_mount(h):
    msc_setup(h)
    assert h.bringup_h_msc_export(1) == 1
    assert h.bringup_h_msc_info(MSC_FAT_OWNER) == 1          # SD_HOST
    assert h.bringup_h_msc_reclaim(1) == 1
    assert h.bringup_h_msc_info(MSC_FAT_OWNER) == 0          # SD_LOCAL
    # The mount is invalidated, so a reader must remount before trusting it.
    assert h.bringup_h_msc_info(MSC_FAT_MOUNTED) == 0
    assert h.bringup_h_msc_info(MSC_MEDIUM_BLOCKS) == 0
    status, body = msc_run(h, cbw(cdb_read10(0, 1), 512))
    assert status == 1 and payload_of(body) == b''


def test_handoff_is_refused_while_the_camera_is_busy(h):
    msc_setup(h)
    # io_idle == 0 means local work is still outstanding.
    assert h.bringup_h_msc_export(0) == 0
    assert h.bringup_h_msc_info(MSC_EXPORTS) == 0
    assert h.bringup_h_msc_info(MSC_HOST_OWNS) == 0
    assert h.bringup_h_msc_export(1) == 1


def test_format_and_eject_are_refused_over_the_endpoints(h):
    msc_setup(h)
    h.bringup_h_msc_export(1)
    assert msc_run(h, cbw(cdb6(0x04), 0))[0] == 1            # FORMAT UNIT
    assert h.bringup_h_msc_sense() >> 8 == SENSE_ILLEGAL_REQUEST
    assert msc_run(h, cbw(cdb6(0x1b, 0, 0, 0, 0x02), 0))[0] == 1   # START STOP + eject
    assert h.bringup_h_card_byte(0) != 0xffffffff            # the card is still there


def test_storage_traffic_does_not_disturb_the_debug_session(h):
    """The two functions share one device and must not share traffic."""
    msc_setup(h)
    h.bringup_h_msc_export(1)
    h.bringup_h_authorize()
    assert query(h, 'g', ax=True).startswith(b'+$00000000')
    status, body = msc_run(h, cbw(cdb6(0x12, 0, 0, 0, 36), 36))
    assert status == 0 and payload_of(body)[8:16] == b'LUMA    '
    assert query(h, 'g', ax=True).startswith(b'+$00000000')


def test_bridge_revokes_only_the_debug_function():
    """The revoke message must be addressed to the vendor interface."""
    import inspect
    source = inspect.getsource(bridge.revoke)
    assert 'interface' in source and '0x5f' in source
    # The storage function belongs to the OS disk driver and is never claimed.
    serve = inspect.getsource(bridge.serve)
    assert 'claim_interface' in serve


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


# C stores these function pointers, so Python must not collect the callbacks
# while a mount is still using them. Nothing here is a firmware concern: it is a
# property of passing Python closures into a C library through ctypes.
_callback_keepalive = []


def keep(callback):
    _callback_keepalive.append(callback)
    return callback


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
    # The caller owns the volume, so ownership and the mount stay in one place.
    volume=C.create_string_buffer(4096)
    result=h.recover_check(volume,keep(card_reader(image)),image.stat().st_size//512*512,
                           C.byref(report))
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
    volume=C.create_string_buffer(4096)
    assert h.recover_check(volume,keep(dead),1<<20,C.byref(report))==codes['RECOVER_NO_FILESYSTEM']
    assert h.recover_check(volume,dead,0,C.byref(report))==codes['RECOVER_NO_CARD']
    assert h.recover_check(volume,C.cast(None,SectorRead),1<<20,C.byref(report))==codes['RECOVER_NO_CARD']
    assert h.recover_check(C.cast(None,C.c_void_p),dead,1<<20,C.byref(report))==codes['RECOVER_NO_CARD']


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
    assert h.recover_check(C.create_string_buffer(4096),read,len(data),C.byref(report))==codes['RECOVER_CANDIDATE_IO']
    assert calls[0]>900


def test_recover_read_only_mount_refuses_creation(h,tmp_path,codes):
    # The property recover_check depends on: a mount with no write callback can
    # never publish a file, so the gate cannot modify the card it inspects.
    image=card_with(tmp_path,[('DESTBIN.BIN',b'x')])
    fat=C.create_string_buffer(4096)
    assert h.fat_mount(fat,keep(card_reader(image)),C.cast(None,SectorWrite),image.stat().st_size)
    assert h.fat_create(fat,b'NEW.BIN',b'y',1)==0
    assert recover(h,image)[0]==codes['RECOVER_CANDIDATE_INVALID']  # 1 byte is not 4 MiB


def test_stream_reader_streams_without_whole_file_buffer(h,tmp_path):
    payload=bytes(range(256))*300
    image=card_with(tmp_path,[('BIG.BIN',payload)])
    read=keep(card_reader(image))
    data=image.read_bytes()
    fat=C.create_string_buffer(4096)
    assert h.fat_mount(fat,keep(read),C.cast(None,SectorWrite),len(data))
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
    read=keep(card_reader(image))
    data=image.read_bytes()
    fat=C.create_string_buffer(4096)
    assert h.fat_mount(fat,keep(read),C.cast(None,SectorWrite),len(data))
    for name in (b'big.bin',b'MISSING.BIN',b'../BIG.BIN',b'TOOLONGNAME.BIN'):
        assert h.fat_stream_open(fat,name,C.create_string_buffer(4096))==0
