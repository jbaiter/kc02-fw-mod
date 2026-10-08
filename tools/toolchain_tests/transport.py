"""Test-specific bounded USB policy; does not change the original dumper globals."""
import struct

import dump_firmware as dump
from .cases import BASE, SIZE, CODE_ADDR, DISPATCH, INPUT, make_packet
from .cache import ICACHE_INIT_FN, ICACHE_SITE


class TestTransport(dump.CameraTransport):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        pins=(dump.MEMORY_TRANSFER_FN,dump.CACHE_CLEAN_FN,dump.SHIM_ADDR,
              dump.CANARY_ADDR,dump.SPI_CHUNK,SIZE)
        if pins!=(0x0204C624,0x0202B058,0x020D8000,0x020D8080,SIZE,SIZE):
            raise dump.DumpError('Dumper addresses differ from the fixed test layout; review integration.')
        self.firmware_ok=False
        self.canary_ok=False
        self.icache_ready=False
        self.profile=None
        self.packet=None
        self.verified={}
        self.last_command=None
        self.last_rx=b''

    def check_firmware(self):
        dump.check_firmware(self)
        address,expected,name=ICACHE_SITE
        if self.read_memory(address,len(expected))!=expected:
            raise dump.DumpError('Unsupported firmware: full '+name+' body differs; refusing RAM execution.')
        self.firmware_ok=True

    def baseline(self):
        self.profile=self.packet=None
        self.upload(dump.CANARY_ADDR,dump.CANARY_BYTES)
        self.upload(BASE,dump.CANARY_PATTERN)
        self.invalidate_code()
        data=self._command(BASE,SIZE,dump.CANARY_ADDR)
        if data!=dump.CANARY_PATTERN:
            raise dump.DumpError('Assembly canary changed its buffer; stop and power-cycle.')
        self.canary_ok=True
        return data

    def invalidate_code(self):
        # Execute unchanged stock code, not a RAM stub that may itself be stale.
        expected=self.verified.get(BASE)
        if expected is None: raise dump.DumpError('Payload readback required before I-cache maintenance.')
        actual=self._command(BASE,SIZE,ICACHE_INIT_FN)
        if actual!=expected:
            raise dump.DumpError('I-cache callback changed its buffer; stop and power-cycle.')
        self.icache_ready=True

    def set_profile(self, entry, packet=None):
        if not self.canary_ok: raise dump.DumpError('Canary has not passed.')
        if entry['kind'] not in ('ram','spi') or not entry['blob']:
            raise dump.DumpError('Invalid fixed test profile.')
        if entry['kind']=='ram' and (packet is None or len(packet)!=SIZE):
            raise dump.DumpError('Missing fixed 2 KiB test packet.')
        if entry['kind']=='ram':
            layout=entry.get('layout')
            nonce,index=struct.unpack_from('<II',packet,layout['input'] if layout else INPUT)
            if index>=len(entry['vectors']) or packet!=make_packet(entry['vectors'][index],nonce,index,layout):
                raise dump.DumpError('Packet differs from the active test/vector profile.')
            if entry['name']=='sfr_read' and entry['vectors'][index]!=[0]:
                raise dump.DumpError('Only read-only SPR_VR selector zero is permitted.')
        self.profile=entry
        self.packet=packet

    def allowed_blobs(self):
        """Every RAM address this transport may write, with its exact expected bytes."""
        blobs={dump.CANARY_ADDR:dump.CANARY_BYTES,BASE:dump.CANARY_PATTERN}
        if self.profile:
            if self.profile['kind']=='ram':
                blobs[dump.SHIM_ADDR]=DISPATCH
                blobs[CODE_ADDR]=self.profile['blob']
                blobs[BASE]=self.packet
            else:
                blobs[dump.SHIM_ADDR]=self.profile['blob']
        return blobs

    def read_region(self, address, length):
        """Chunked read; every USB transfer stays at or below SIZE bytes."""
        data=b''
        while len(data)<length:
            data+=self._command(address+len(data),min(SIZE,length-len(data)))
        return data

    def read_memory(self, address, length):
        return self.read_region(address,length)

    def upload(self, address, blob):
        """Chunked write followed by a chunked read-back of the same bytes."""
        for start in range(0,len(blob),SIZE):
            chunk=blob[start:start+SIZE]
            self._command(address+start,len(chunk),dump.CACHE_CLEAN_FN,payload=chunk)
        if self.read_region(address,len(blob))!=blob:
            raise dump.DumpError('RAM readback mismatch at 0x%08x; refusing execution.' % address)
        self.verified[address]=blob

    def _approved_write(self, blobs, buffer, payload):
        for address,blob in blobs.items():
            start=buffer-address
            if 0<=start<len(blob) and blob[start:start+len(payload)]==payload:
                return address
        return None

    def _approved_read(self, blobs, buffer, length):
        for address,blob in blobs.items():
            start=buffer-address
            if 0<=start<len(blob) and length==min(SIZE,len(blob)-start):
                return address
        return None

    def _command(self, buffer, length, callback=dump.CALLBACK_SKIP, offset=0, payload=None):
        if not self.claimed: raise dump.DumpError('USB interface has not been claimed.')
        blobs=self.allowed_blobs()
        direction=0x80 if payload is None else 0
        if payload is not None:
            if not self.firmware_ok: raise dump.DumpError('Firmware signatures not checked.')
            if not 0<length<=SIZE:
                raise dump.DumpError('RAM write exceeds the %d-byte transfer size.' % SIZE)
            if callback!=dump.CACHE_CLEAN_FN or offset:
                raise dump.DumpError('Invalid RAM write callback.')
            written=self._approved_write(blobs,buffer,payload)
            if written is None:
                raise dump.DumpError('Write differs from the active fixed test blob.')
            self.verified.pop(written,None)
            self.icache_ready=False
        elif callback==dump.CALLBACK_SKIP:
            sites={(a,len(b)) for a,b,_ in (*dump.ROUTINE_SIGNATURES,ICACHE_SITE)}
            if offset or ((buffer,length) not in sites and
                          self._approved_read(blobs,buffer,length) is None):
                raise dump.DumpError('Memory read outside approved sites.')
        elif callback==ICACHE_INIT_FN:
            if not self.firmware_ok or buffer!=BASE or length!=SIZE or offset or self.verified.get(BASE)!=blobs[BASE]:
                raise dump.DumpError('Invalid/unverified instruction-cache maintenance request.')
        elif callback==dump.CANARY_ADDR:
            if not self.firmware_ok or not self.icache_ready or buffer!=BASE or length!=SIZE or offset:
                raise dump.DumpError('Invalid canary execution.')
            if self.verified.get(dump.CANARY_ADDR)!=dump.CANARY_BYTES or \
                    self.verified.get(BASE)!=dump.CANARY_PATTERN:
                raise dump.DumpError('Canary blobs not verified.')
        elif callback==dump.SHIM_ADDR:
            if not self.firmware_ok or not self.canary_ok or not self.icache_ready or not self.profile or buffer!=BASE or length!=SIZE:
                raise dump.DumpError('Invalid test execution.')
            if self.verified.get(dump.SHIM_ADDR)!=blobs[dump.SHIM_ADDR] or \
                    self.verified.get(BASE)!=blobs[BASE]:
                raise dump.DumpError('Active code/input blobs not verified.')
            if offset not in ((0,) if self.profile['kind']=='ram' else (0,2048)):
                raise dump.DumpError('Test parameter/flash offset not permitted.')
        else:
            raise dump.DumpError('Callback is not permitted.')
        if buffer==BASE and length!=SIZE:
            raise dump.DumpError('Payload buffer transfers must be exactly %d bytes.' % SIZE)
        self.tag=(self.tag+1)&0xffffffff
        cdb=bytes([0xcd])+struct.pack('<III',dump.MEMORY_TRANSFER_FN,buffer,callback)+offset.to_bytes(3,'little')
        cbw=struct.pack('<4sIIBBB',b'USBC',self.tag,length,direction,0,16)+cdb
        self.last_command=dict(tag=self.tag,buffer=buffer,length=length,callback=callback,
                               parameter=offset,direction=direction)
        self.last_rx=b''
        self.log('test CBW '+str(self.last_command))
        self._write(cbw)
        if payload is None:
            data=self._read_exact(length); self.last_rx=data
        else:
            self._write(payload); data=b''
        csw=self._read_exact(13)
        signature,tag,residue,status=struct.unpack('<4sIIB',csw)
        if (signature,tag,residue,status)!=(b'USBS',self.tag,0,0):
            raise dump.DumpError('Invalid USB status; stop and power-cycle: '+str((signature,tag,residue,status)))
        if callback in (dump.SHIM_ADDR,dump.CANARY_ADDR):
            # Another execution requires another verified upload of both regions.
            self.verified.pop(BASE,None)
            self.verified.pop(CODE_ADDR,None)
        return data
