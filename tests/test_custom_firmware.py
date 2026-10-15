"""Tests of the new guest sources, not stock patches or a host-only UI mock."""
import ctypes as C
import importlib.util
import io
from pathlib import Path
import random
import shutil
import struct
import subprocess
import zlib

import pytest
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT/'custom-firmware'
U8, U16, U32 = C.c_ubyte, C.c_ushort, C.c_uint
P8 = C.POINTER(U8)
Read = C.CFUNCTYPE(C.c_int,U32,P8)
Write = C.CFUNCTYPE(C.c_int,U32,P8)

class Theme(C.Structure):
    _fields_ = [(n,U16) for n in ('bg','panel','text','muted','accent')] + [
        ('boot_ms',U32),('shutter_ms',U32),('boot',C.c_char*13),('background',C.c_char*13)]

class Input(C.Structure):
    _fields_ = [('candidate',C.c_int),('stable',C.c_int),('changed_at',U32),('down_at',U32),('held',C.c_int)]

class Fat(C.Structure):
    _fields_ = [('read',Read),('write',Write)] + [(n,U32) for n in
        ('capacity','fat','root','data','total','clusters')] + [
        ('fat_sectors',U16),('root_entries',U16),('spc',U8),('fats',U8),
        ('mounted',C.c_int),('writable',C.c_int),('owner',C.c_int)]

class App(C.Structure):
    _fields_ = [(n,C.c_uint) for n in ('screen','ink','setting','photo','count','boot_at','shutter_at','notice_until',
                                    'booting','shutter','lens')] + [('notice',C.c_char*40),('theme',Theme)]

class Platform(C.Structure):
    # NULL callbacks deliberately exercise unavailable devices.
    _fields_ = [(n,C.c_void_p) for n in ('read','save','load','count','print','usb','update','lens')]

def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    obj=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj

@pytest.fixture(scope='module')
def native(tmp_path_factory):
    if not shutil.which('cc'):
        pytest.skip('C compiler unavailable')
    out=tmp_path_factory.mktemp('luma-core')/'core.so'
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-O1','-g',
                    '-fsanitize=undefined','-fno-sanitize-recover=undefined','-shared','-fPIC',
                    '-I'+str(FW/'include'),*(str(FW/'src'/n) for n in ('theme.c','input.c','storage.c','ui.c','paint.c','debug.c','update.c','crc.c')),
                    str(FW/'tests/rsp_harness.c'), '-o',str(out)],check=True)
    lib=C.CDLL(str(out))
    lib.theme_default.argtypes=[C.POINTER(Theme)]
    lib.theme_parse.argtypes=[C.POINTER(Theme),C.c_void_p,C.c_uint]
    lib.bmp_read.argtypes=[C.c_void_p,C.c_uint,C.POINTER(U16)]
    lib.bmp_write.argtypes=[C.POINTER(U16),C.c_void_p,C.c_uint]
    lib.input_poll.argtypes=[C.POINTER(Input),C.c_int,U32,C.POINTER(C.c_int)]
    lib.fat_mount.argtypes=[C.POINTER(Fat),Read,Write,U32]
    lib.fat_read.argtypes=[C.POINTER(Fat),C.c_char_p,C.c_void_p,C.c_uint,C.POINTER(C.c_uint)]
    lib.fat_create.argtypes=[C.POINTER(Fat),C.c_char_p,C.c_void_p,C.c_uint]
    lib.fat_photos.argtypes=[C.POINTER(Fat),C.POINTER(C.c_uint),C.c_uint]
    lib.sd_handoff.argtypes=[C.POINTER(Fat),C.c_int,C.c_int]
    lib.crc32.argtypes=[C.c_void_p,C.c_uint]
    lib.crc32.restype=U32
    lib.print_row.argtypes=[C.POINTER(U16),C.c_uint,C.c_int,P8]
    lib.blend.argtypes=[U16,U16,C.c_uint]
    lib.blend.restype=U16
    lib.box.argtypes=[C.POINTER(U16),C.c_int,C.c_int,C.c_int,C.c_int,U16]
    lib.round_box.argtypes=[C.POINTER(U16),C.c_int,C.c_int,C.c_int,C.c_int,C.c_int,U16]
    lib.ring.argtypes=[C.POINTER(U16),C.c_int,C.c_int,C.c_int,C.c_int,U16]
    lib.ring8.argtypes=lib.ring.argtypes
    lib.app_init.argtypes=[C.POINTER(App),C.POINTER(Platform),U32]
    lib.app_event.argtypes=[C.POINTER(App),C.POINTER(Platform),C.c_int,C.c_int,U32,C.POINTER(U16),C.POINTER(U16)]
    lib.app_render.argtypes=[C.POINTER(App),C.POINTER(U16),C.POINTER(U16),C.POINTER(U16),C.POINTER(U16),C.POINTER(U16),U32]
    lib.harness_init.argtypes=[C.c_int]
    lib.harness_feed.argtypes=[C.c_void_p,C.c_uint]
    lib.harness_output.argtypes=[P8]
    lib.image_validate.argtypes=[C.c_void_p,U32,C.c_void_p]
    lib.boot_select.argtypes=[C.c_void_p,C.c_void_p]
    lib.update_apply.argtypes=[C.c_void_p,C.c_void_p,U32,C.c_int,C.c_int]
    return lib


def test_theme_colors_and_bounds(native):
    t=Theme()
    b=b'accent = #a4edbf\nbackground=#0c1218\nboot_ms=3000\nboot_image=BOOT.BMP\n'
    assert native.theme_parse(C.byref(t),b,len(b))
    assert t.bg == ((12&248)<<8)|((18&252)<<3)|(24>>3)
    assert t.accent == ((164&248)<<8)|((237&252)<<3)|(191>>3)
    assert t.boot_ms==3000 and t.boot==b'BOOT.BMP'

@pytest.mark.parametrize('bad',[
    b'unknown=1',b'boot_ms=0',b'boot_ms=99999999999999',b'shutter_ms=99',
    b'accent=#ff',b'accent=#gg0000',b'boot_image=../X.BMP',b'boot_image=/X.BMP',
    b'boot_image=X.JPG',b'background_image=TOOLONGXX.BMP',b'background=red',
    b'panel=#000000\0',b'panel=#000000\x80',b'novalue',b'x'*5000])
def test_theme_rejected_atomically(native,bad):
    t=Theme()
    native.theme_default(C.byref(t))
    before=bytes(t)
    assert not native.theme_parse(C.byref(t),bad,len(bad))
    assert bytes(t)==before


def test_theme_fuzz_bounded(native):
    rng=random.Random(3292)
    t=Theme()
    for _ in range(500):
        b=rng.randbytes(rng.randrange(4097))
        native.theme_parse(C.byref(t),b,len(b))


def test_bmp_roundtrip_and_corrupt_headers(native):
    im=Image.new('RGB',(320,240),(165,235,189))
    src=io.BytesIO(); im.save(src,'BMP'); blob=src.getvalue()
    pixels=(U16*(320*240))()
    assert native.bmp_read(blob,len(blob),pixels)
    out=C.create_string_buffer(len(blob))
    assert native.bmp_write(pixels,out,len(blob))==len(blob)
    result=Image.open(io.BytesIO(out.raw))
    assert result.size==(320,240) and result.getpixel((0,0))==(164,234,189)
    # Valid negative height uses top-down row order.
    top=bytearray(blob); struct.pack_into('<i',top,22,-240)
    assert native.bmp_read(bytes(top),len(top),pixels)
    for offset,data in [(0,b'XX'),(14,struct.pack('<I',108)),(18,struct.pack('<I',0xffffffff)),
                        (22,struct.pack('<I',0x80000000)),(28,b'\x20\0'),(30,b'\1\0\0\0'),
                        (10,struct.pack('<I',0xfffffff0))]:
        b=bytearray(blob); b[offset:offset+len(data)]=data
        assert not native.bmp_read(bytes(b),len(b),pixels)
    assert not native.bmp_read(blob,53,pixels)
    assert not native.bmp_read(blob,len(blob)-1,pixels)
    assert not native.bmp_write(pixels,out,len(blob)-1)


def test_input_debounce_and_hold_no_shutter(native):
    i=Input(); held=C.c_int()
    poll=lambda raw,t:native.input_poll(C.byref(i),raw,t,C.byref(held))
    assert poll(3,0)==0
    assert poll(0,10)==0  # bounce
    assert poll(3,20)==0
    assert poll(3,50)==0
    assert poll(3,699)==0
    assert poll(3,700)==3 and held.value==1
    assert poll(3,900)==0
    assert poll(0,950)==0
    assert poll(0,980)==0  # release consumed, no shutter
    assert poll(3,1000)==0
    assert poll(3,1030)==0
    assert poll(0,1110)==0
    assert poll(0,1140)==3 and held.value==0


def test_render_state_and_shared_print_policy(native):
    a=App(); p=Platform()
    pixels=(U16*(320*240))()
    # Guard both sides of render target against writes outside the framebuffer.
    guarded=(U16*(320*240+16))(*([0x55aa]*(320*240+16)))
    target=C.cast(C.byref(guarded,16),C.POINTER(U16))
    native.app_init(C.byref(a),C.byref(p),0)
    assert a.ink==0
    native.app_render(C.byref(a),target,pixels,pixels,None,None,1500)
    assert not a.booting
    native.app_event(C.byref(a),C.byref(p),2,0,1600,pixels,pixels)
    assert a.ink==1
    native.app_event(C.byref(a),C.byref(p),4,0,1700,pixels,pixels)
    assert a.screen==1 and a.ink==1
    native.app_event(C.byref(a),C.byref(p),3,1,2400,pixels,pixels)
    assert a.ink==2 and not a.shutter
    native.app_event(C.byref(a),C.byref(p),4,0,2500,pixels,pixels)
    assert a.screen==2
    native.app_event(C.byref(a),C.byref(p),2,0,2600,pixels,pixels)
    assert a.setting==1
    native.app_event(C.byref(a),C.byref(p),3,0,2700,pixels,pixels)
    assert b'not ready' in a.notice
    for screen in range(3):
        a.screen=screen
        native.app_render(C.byref(a),target,pixels,pixels,None,None,3000)
    assert list(guarded[:8])==[0x55aa]*8 and list(guarded[-8:])==[0x55aa]*8


def test_shape_antialiasing_symmetry_and_no_blur(native):
    pixels=(U16*(320*240))()
    native.ring(pixels,80,80,12,2,0xffff)
    values=set(pixels)
    assert 0 in values and 0xffff in values and len(values)>4
    assert pixels[80*320+80]==0  # Hole remains untouched.
    for y in range(-13,14):
        for x in range(-13,14):
            assert pixels[(80+y)*320+80+x]==pixels[(80-y)*320+80-x]
    # Antialiasing only at the shape boundary; solid interiors stay solid.
    native.round_box(pixels,140,60,60,40,8,0xffff)
    assert pixels[80*320+170]==0xffff
    assert pixels[60*320+140]==0
    assert pixels[59*320+170]==0
    corner=[pixels[y*320+x] for y in range(60,68) for x in range(140,148)]
    assert any(c not in (0,0xffff) for c in corner)
    for y in range(40):
        for x in range(60):
            assert pixels[(60+y)*320+140+x]==pixels[(99-y)*320+199-x]
    # A distant sharp pixel does not change: there is no screen-wide filter.
    pixels[200*320+250]=0xf800
    native.ring(pixels,80,80,13,2,0xffff)
    assert pixels[200*320+250]==0xf800
    assert native.blend(0,0xffff,0)==0
    assert native.blend(0,0xffff,255)==0xffff
    assert native.blend(0,0xffff,0xffffffff)==0xffff


def test_boot_ring_moves_in_subpixels(native):
    integer=(U16*(320*240))()
    fixed=(U16*(320*240))()
    native.ring(integer,80,80,12,2,0xffff)
    native.ring8(fixed,80,80,12*8,2*8,0xffff)
    assert bytes(integer)==bytes(fixed)
    frames=[]
    for radius8 in range(96,104):
        f=(U16*(320*240))()
        native.ring8(f,80,80,radius8,16,0xffff)
        frames.append(bytes(f))
    assert len(set(frames))>=6  # More than one integer-sized jump per pixel.


def test_shape_clipping_and_invalid_geometry(native):
    guarded=(U16*(320*240+16))(*([0x55aa]*(320*240+16)))
    pixels=C.cast(C.byref(guarded,16),C.POINTER(U16))
    for x,y in ((-12,-12),(315,235),(0,0),(319,239)):
        native.round_box(pixels,x,y,20,20,9,0xffff)
        native.ring(pixels,x,y,16,1,0)
    before=bytes(guarded)
    for v in (-2147483648,2147483647):
        native.round_box(pixels,v,v,20,20,8,0)
        native.box(pixels,0,0,v,v,0)
        native.ring(pixels,0,0,v,2,0)
    assert bytes(guarded)==before
    assert list(guarded[:8])==[0x55aa]*8 and list(guarded[-8:])==[0x55aa]*8


def test_print_modes_and_crc(native):
    pixels=(U16*(320*240))()
    dots=(U8*384)()
    for x in range(320*240):
        g=x%320*255//319
        pixels[x]=((g&248)<<8)|((g&252)<<3)|(g>>3)
    for mode in range(3):
        native.print_row(pixels,100,mode,dots)
        levels=set(dots)
        assert levels==({0} if mode==0 else {0,31} if mode==1 else set(range(32)))
    native.print_row(pixels,288,2,dots)
    assert set(dots)=={0}
    assert native.crc32(b'123456789',9)==zlib.crc32(b'123456789')

@pytest.fixture
def card(tmp_path,native):
    seed=module('luma_card',FW/'tools/card.py')
    image=seed.create(tmp_path/'sd.img',demo=True)
    file=image.open('r+b')
    failed={'after':None,'writes':0}
    accesses=[]
    @Read
    def read(lba,out):
        accesses.append(lba)
        file.seek(lba*512)
        b=file.read(512)
        if len(b)!=512:
            return 0
        C.memmove(out,b,512)
        return 1
    @Write
    def write(lba,data):
        if failed['after'] is not None and failed['writes']>=failed['after']:
            return 0
        failed['writes']+=1
        file.seek(lba*512); file.write(C.string_at(data,512)); file.flush()
        return 1
    fs=Fat()
    assert native.fat_mount(C.byref(fs),read,write,image.stat().st_size//512)
    yield fs,file,read,write,failed,accesses
    file.close()


def test_fat_create_read_mirrors_and_never_overwrite(native,card):
    fs,file,read,write,_,_=card
    ids=(C.c_uint*64)()
    assert native.fat_photos(C.byref(fs),ids,64)==2 and list(ids[:2])==[1,2]
    payload=b'Luma capture\0'*123
    assert native.fat_create(C.byref(fs),b'LUM00003.BMP',payload,len(payload))
    assert not native.fat_create(C.byref(fs),b'LUM00003.BMP',b'overwrite',9)
    out=C.create_string_buffer(4096); size=C.c_uint()
    assert native.fat_read(C.byref(fs),b'LUM00003.BMP',out,len(out),C.byref(size))
    assert out.raw[:size.value]==payload
    file.seek(fs.fat*512); a=file.read(fs.fat_sectors*512)
    file.seek((fs.fat+fs.fat_sectors)*512); b=file.read(fs.fat_sectors*512)
    assert a==b
    # Independent Python FAT reader validates the newly published chain.
    reader=module('luma_fat_reader',ROOT/'tools/emu/sd_read.py')
    with reader.FAT16(Path(file.name)) as fat:
        assert fat.read_file('LUM00003.BMP')==payload
    assert not native.fat_read(C.byref(fs),b'../X.BMP',out,len(out),C.byref(size))
    assert not native.fat_create(C.byref(fs),b'X/B.BMP',payload,len(payload))

@pytest.mark.parametrize('writes_before_failure',[0,1,3,4,5,8])
def test_failed_writes_lock_mount_and_do_not_publish(native,card,writes_before_failure):
    fs,file,read,write,fail,_=card
    fail['after']=writes_before_failure
    payload=b'a'*2000
    assert not native.fat_create(C.byref(fs),b'NEW.BMP',payload,len(payload))
    assert not fs.writable
    out=C.create_string_buffer(4096); size=C.c_uint()
    assert not native.fat_read(C.byref(fs),b'NEW.BMP',out,len(out),C.byref(size))
    # Existing data survives, even when an unpublished chain is orphaned.
    assert native.fat_read(C.byref(fs),b'THEME.INI',out,len(out),C.byref(size))


def test_storage_ownership_and_host_remount(native,card):
    fs,_,_,_,_,_=card
    assert not native.sd_handoff(C.byref(fs),1,0)
    assert not native.sd_handoff(C.byref(fs),-1,1)
    assert native.sd_handoff(C.byref(fs),1,1)
    out=C.create_string_buffer(4096); size=C.c_uint()
    assert not native.fat_read(C.byref(fs),b'THEME.INI',out,len(out),C.byref(size))
    assert not native.fat_create(C.byref(fs),b'X.TXT',b'x',1)
    assert not native.sd_handoff(C.byref(fs),2,1)
    assert native.sd_handoff(C.byref(fs),0,1)
    assert not fs.mounted and not fs.writable  # Must re-read card after host eject.


def test_bad_bpb_and_disagreeing_fats(native,card):
    fs,file,read,write,_,accesses=card
    file.seek((fs.fat+fs.fat_sectors)*512+4); file.write(b'\x01'); file.flush()
    assert not native.fat_mount(C.byref(fs),read,write,file.seek(0,2)//512)
    assert max(accesses)<file.seek(0,2)//512


def test_card_create_only(tmp_path):
    seed=module('luma_card_create',FW/'tools/card.py')
    p=seed.create(tmp_path/'sd.img')
    first=p.stat().st_size
    with pytest.raises(FileExistsError):
        seed.create(p)
    assert p.stat().st_size==first


def rsp_packet(payload):
    if isinstance(payload,str):
        payload=payload.encode()
    return b'$'+payload+b'#'+f'{sum(payload)&255:02x}'.encode()


def rsp_exchange(native,payload):
    packet=rsp_packet(payload)
    native.harness_feed(packet,len(packet))
    out=(U8*16384)()
    n=native.harness_output(out)
    data=bytes(out[:n])
    assert data.startswith(b'+$')
    end=data.index(b'#',2)
    body=data[2:end]
    assert int(data[end+1:end+3],16)==sum(body)&255
    return body


def test_rsp_exact_register_packet_and_little_endian(native):
    native.harness_init(1)
    regs=rsp_exchange(native,'g')
    assert len(regs)==35*8
    assert regs[:16]==b'0000000004030201'
    assert regs[33*8:34*8]==b'00010002'  # npc=0x02000100
    assert rsp_exchange(native,'p1')==b'04030201'
    assert rsp_exchange(native,'P1=78563412')==b'OK'
    assert rsp_exchange(native,'p1')==b'78563412'
    assert rsp_exchange(native,'P0=01000000')==b'E02'
    assert rsp_exchange(native,'p23')==b'E01'  # register 35 outside packet
    assert rsp_exchange(native,'P1=short')==b'E01'


def test_rsp_authorisation_and_memory_bounds(native):
    native.harness_init(0)
    assert b'PacketSize=400' in rsp_exchange(native,'qSupported')
    assert rsp_exchange(native,'g')==b'E22'
    assert rsp_exchange(native,'M0,1:ff')==b'E22'
    native.harness_init(1)
    assert rsp_exchange(native,'m0,4')==b'00010203'
    assert rsp_exchange(native,'M0,4:deadbeef')==b'OK'
    assert rsp_exchange(native,'m0,4')==b'deadbeef'
    for query in ('mffffffff,2','m0,101','m400,1','m100000000,1','m0,0x4','M0,1:ff00','M0,1:zz'):
        assert rsp_exchange(native,query) in (b'E01',b'E02')
    assert rsp_exchange(native,'s')==b'E02'
    assert native.harness_runs()==0
    assert rsp_exchange(native,'vCont?')==b''  # Never advertise fake stepping.


def test_rsp_xml_matches_packet_and_recovers_bad_frames(native):
    import xml.etree.ElementTree as ET
    native.harness_init(1)
    xml=b''
    while True:
        part=rsp_exchange(native,f'qXfer:features:read:target.xml:{len(xml):x},100')
        assert part[:1] in (b'm',b'l')
        xml+=part[1:]
        if part[:1]==b'l':
            break
    registers=ET.fromstring(xml).findall('.//reg')
    assert [e.attrib['name'] for e in registers]==[f'r{i}' for i in range(32)]+['ppc','npc','sr']
    out=(U8*16384)()
    native.harness_feed(b'$g#00',5)
    assert native.harness_output(out)==1 and out[0]==ord('-')
    oversized=rsp_packet(b'x'*2048)
    native.harness_feed(oversized,len(oversized))
    assert native.harness_output(out)==1 and out[0]==ord('-')
    rng=random.Random(3392)
    for _ in range(100):
        b=rng.randbytes(200)
        native.harness_feed(b,len(b))
        native.harness_output(out)
    assert rsp_exchange(native,'?')==b'S05'


def test_rsp_with_real_gdb(native):
    """Actual debugger handshake with a host stop-monitor harness; NOT USB."""
    import socket
    import threading
    gdb=shutil.which('gdb-multiarch')
    cached=Path.home()/'.cache/kc02-gdb/root/usr/bin/gdb-multiarch'
    if not gdb and cached.is_file():
        gdb=str(cached)
    if not gdb:
        pytest.skip('OpenRISC-capable GDB unavailable')
    native.harness_init(1)
    listener=socket.socket()
    listener.bind(('127.0.0.1',0))
    listener.listen(1)
    listener.settimeout(10)
    errors=[]
    def server():
        try:
            conn,_=listener.accept()
            with conn:
                conn.settimeout(10)
                while data:=conn.recv(4096):
                    native.harness_feed(data,len(data))
                    out=(U8*16384)()
                    if n:=native.harness_output(out):
                        conn.sendall(bytes(out[:n]))
        except Exception as exc:
            errors.append(exc)
        finally:
            listener.close()
    thread=threading.Thread(target=server,daemon=True)
    port=listener.getsockname()[1]
    thread.start()
    result=subprocess.run([gdb,'-q','-nx','-nh','-batch',
                           '-ex','set architecture or1k','-ex','set endian little',
                           '-ex',f'target remote 127.0.0.1:{port}',
                           '-ex','p/x $r1','-ex','p/x $npc','-ex','x/4bx 0','-ex','detach'],
                          text=True,capture_output=True,timeout=15)
    thread.join(11)
    assert not errors and not thread.is_alive()
    assert result.returncode==0, result.stdout+result.stderr
    assert '0x1020304' in result.stdout and '0x2000100' in result.stdout
    assert 'Truncated register' not in result.stderr
    assert 'rejected' not in result.stderr.lower()


ByteRead=C.CFUNCTYPE(C.c_int,C.c_void_p,U32,P8,C.c_uint)
Erase=C.CFUNCTYPE(C.c_int,C.c_void_p,U32)
Program=C.CFUNCTYPE(C.c_int,C.c_void_p,U32,P8,C.c_uint)
class Source(C.Structure):
    _fields_=[('context',C.c_void_p),('read',ByteRead),('size',U32)]
class Nor(C.Structure):
    _fields_=[('context',C.c_void_p),('read',ByteRead),('erase',Erase),('program',Program)]
class BootRecord(C.Structure):
    _fields_=[(n,U32) for n in ('generation','slot','bytes','crc','version')]
class ImageInfo(C.Structure):
    _fields_=[(n,U32) for n in ('bytes','entry','version','crc','boards')]


def update_image(payload,version=2,boards=7,flags=0):
    header=bytearray(64)
    header[:8]=b'LUMAIMG\0'
    struct.pack_into('<HH8I',header,8,1,64,boards,len(payload),0x02000000,0x02000000,
                     version,zlib.crc32(payload),0,flags)
    struct.pack_into('<I',header,36,zlib.crc32(header))
    return bytes(header)+payload


def image_source(image):
    @ByteRead
    def read(ctx,offset,out,size):
        if offset>len(image) or size>len(image)-offset:
            return 0
        C.memmove(out,image[offset:offset+size],size)
        return 1
    return Source(None,read,len(image))


def nor_model(fail_at=None,partial=0):
    flash=bytearray(b'\xff')*0x400000
    previous=b'last known bootable application'*13
    flash[0x20000:0x20000+len(previous)]=previous
    record=b'LMCOMMIT'+struct.pack('<5I',1,0,len(previous),zlib.crc32(previous),1)
    flash[0x10000:0x10020]=record+struct.pack('<I',zlib.crc32(record))
    writes=[]
    @ByteRead
    def read(ctx,offset,out,size):
        if offset>len(flash) or size>len(flash)-offset:
            return 0
        C.memmove(out,bytes(flash[offset:offset+size]),size)
        return 1
    @Erase
    def erase(ctx,offset):
        if offset<0x10000 or offset%4096 or offset>len(flash)-4096:
            return 0
        writes.append(('erase',offset,4096))
        if fail_at is not None and len(writes)-1>=fail_at:
            flash[offset:offset+partial]=b'\xff'*partial
            return 0
        flash[offset:offset+4096]=b'\xff'*4096
        return 1
    @Program
    def program(ctx,offset,buf,size):
        if offset<0x10000 or not size or (offset%256)+size>256 or offset>len(flash)-size:
            return 0
        writes.append(('program',offset,size))
        failed=fail_at is not None and len(writes)-1>=fail_at
        data=C.string_at(buf,size)
        for i,b in enumerate(data[:min(partial,size) if failed else size]):
            flash[offset+i]&=b  # NOR only clears bits; erase restores ones.
        return not failed
    return Nor(None,read,erase,program),flash,previous,writes


def selected(native,nor):
    record=BootRecord()
    assert native.boot_select(C.byref(nor),C.byref(record))
    return record


def test_update_validation_and_development_rejection(native):
    payload=b'new image'*100
    info=ImageInfo()
    for board in (1,2,4):
        source=image_source(update_image(payload))
        assert native.image_validate(C.byref(source),board,C.byref(info))
        assert info.bytes==len(payload) and info.version==2
    for blob in (update_image(payload,boards=1),update_image(payload,flags=1),
                 update_image(payload)+b'extra',update_image(payload)[:-1],
                 update_image(payload)[:100]+b'corrupt'):
        source=image_source(blob)
        assert not native.image_validate(C.byref(source),4,C.byref(info))
    rng=random.Random(3292)
    original=update_image(payload)
    for _ in range(100):
        blob=bytearray(original)
        blob[rng.randrange(len(blob))]^=1
        source=image_source(bytes(blob))
        assert not native.image_validate(C.byref(source),1,C.byref(info))


def test_update_requires_recovery_and_physical_confirmation(native):
    source=image_source(update_image(b'new application'))
    nor,flash,old,writes=nor_model()
    for consent,recovery in ((0,0),(1,0),(0,1)):
        assert native.update_apply(C.byref(nor),C.byref(source),1,consent,recovery)==1
        assert not writes
    stale=image_source(update_image(b'old',version=1))
    assert native.update_apply(C.byref(nor),C.byref(stale),1,1,1)==5
    assert not writes
    flash[0x10000:0x10020]=b'\xff'*32
    assert native.update_apply(C.byref(nor),C.byref(source),1,1,1)==4
    assert not writes


def test_update_commit_then_boot_alternate_slot(native):
    payload=b'new application code'*53
    source=image_source(update_image(payload))
    nor,flash,old,writes=nor_model()
    assert native.update_apply(C.byref(nor),C.byref(source),4,1,1)==0
    record=selected(native,nor)
    assert (record.slot,record.generation,record.version)==(1,2,2)
    assert flash[0x1a0000:0x1a0000+len(payload)]==payload
    assert flash[0x20000:0x20000+len(old)]==old
    assert all(offset>=0x10000 and not (0x20000<=offset<0x1a0000) for _,offset,_ in writes)
    # Losing one byte of the newer slot falls back to the earlier bootable one.
    flash[0x1a0000]^=1
    assert selected(native,nor).slot==0


def test_update_power_cut_every_mutation_and_partial_commit(native):
    source=image_source(update_image(b'new firmware'*71))
    nor,_,_,writes=nor_model()
    assert native.update_apply(C.byref(nor),C.byref(source),1,1,1)==0
    total=len(writes)
    for cut in range(total):
        nor,flash,old,trace=nor_model(fail_at=cut,partial=3)
        assert native.update_apply(C.byref(nor),C.byref(source),1,1,1)==3
        assert selected(native,nor).slot==0
        assert flash[0x20000:0x20000+len(old)]==old
    # Interrupt the final commit record at every byte, including a lost ACK
    # after all bytes reached NOR. Selection is always old OR fully committed.
    for partial in range(33):
        nor,flash,old,trace=nor_model(fail_at=total-1,partial=partial)
        assert native.update_apply(C.byref(nor),C.byref(source),1,1,1)==3
        record=selected(native,nor)
        assert record.slot==(1 if partial==32 else 0)
        assert flash[0x20000:0x20000+len(old)]==old
