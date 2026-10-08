"""Offline USB policy/oracle tests. No PyUSB, camera or compiler is required."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS=Path(__file__).resolve().parents[1]/'tools'
sys.path.insert(0,str(TOOLS))
import test_toolchain as runner
from toolchain_tests import cases, build
from toolchain_tests.transport import TestTransport
from toolchain_tests.cache import ICACHE_INIT_FN, ICACHE_SIGNATURE, ICACHE_SITE
from test_dump_firmware import FakeCamera, fake_usb, image

dump=runner.dump


def reference():
    result=bytearray(image())
    for address,signature,_ in (*dump.ROUTINE_SIGNATURES,ICACHE_SITE):
        off=address-0x02000000+0x2600
        result[off:off+len(signature)]=signature
    return bytes(result)


def entry(name='echo', ident=33):
    c=cases.BY_NAME[name]
    blob=bytearray(dump.CANARY_PATTERN)
    struct.pack_into('<I',blob,0,ident)
    return dict(name=name,key=name+'-Os',id=ident,kind='ram',blob=bytes(blob),
                status='ready',vectors=cases.vectors(c,count=0),symbols={})


def make_response(e,packet):
    """Fake device for protocol tests ONLY; the raw-word model tests actual code."""
    data=bytearray(packet)
    nonce,index=struct.unpack_from('<II',packet,cases.INPUT)
    result=cases.expected(cases.BY_NAME[e['name']],e['vectors'][index],e,nonce)
    if e['name']=='sfr_read': result=[0x87654321]*3
    if e['name']=='spr_map': result=[0x12345678]*3+[1]+[0xc003]*3+[1]+[0x90003]*3+[1]+[0x201]*3+[1]
    if e['name']=='preserved':
        result[0]=result[1]=0x123400; result[17]=result[0]+80
        result[12]=result[13]=0x1111; result[14]=result[15]=0x2222
    struct.pack_into('<8I',data,cases.HEADER,cases.MAGIC,e['id'],nonce,cases.SIZE,0,len(result),cases.BASE,index)
    for i,x in enumerate(result): struct.pack_into('<I',data,cases.OUTPUT+4*i,x)
    return bytes(data)


class TestCamera(FakeCamera):
    def __init__(self, entries=()):
        super().__init__()
        self.data=reference()
        self.memory[ICACHE_INIT_FN]=ICACHE_SIGNATURE
        self.entries={e['blob']:e for e in entries}
        self.executions=0
        self.cached_shim=None
        self.fault=None
        self.fail_execution=1
        self.corrupt_upload=False

    def read_code(self):
        """Reassemble the code region from the chunks uploaded to CODE_ADDR."""
        out=bytearray(); address=cases.CODE_ADDR
        while True:
            chunk=self.memory.get(address)
            if not chunk: break
            out+=chunk; address+=len(chunk)
        return bytes(out)

    def write(self,endpoint,data,timeout):
        if self.pending:
            address=self.pending[1]
            result=super().write(endpoint,data,timeout)
            if self.corrupt_upload and address in (cases.BASE,cases.CODE_ADDR):
                b=bytearray(self.memory[address]); b[0]^=1; self.memory[address]=bytes(b)
            return result
        signature,tag,length,direction,lun,cdb_len=struct.unpack('<4sIIBBB',data[:15])
        opcode,function,address,callback=struct.unpack('<BIII',data[15:28])
        offset=int.from_bytes(data[28:31],'little')
        if direction==0x80 and callback==ICACHE_INIT_FN:
            self.cached_shim=None
        if direction==0x80 and callback==dump.SHIM_ADDR:
            if self.cached_shim is None: self.cached_shim=self.memory.get(dump.SHIM_ADDR)
            if self.cached_shim==dump.SHIM_BYTES and self.memory.get(dump.SHIM_ADDR)!=dump.SHIM_BYTES:
                # Reproduce the live failure: RAM readback is new, CPU fetch is old.
                self.commands.append((address,length,direction,callback,offset))
                result=self.data[offset:offset+length]
                self.memory[address]=result
                self.queue.extend((result,self.csw(tag)))
                return len(data)
        if direction==0x80 and callback==dump.SHIM_ADDR and self.memory.get(dump.SHIM_ADDR)==cases.DISPATCH:
            self.commands.append((address,length,direction,callback,offset))
            assert function==dump.MEMORY_TRANSFER_FN and opcode==0xcd and offset==0
            packet=self.memory[address]
            e=self.entries[self.memory.get(cases.CODE_ADDR)]
            result=make_response(e,packet); self.executions+=1
            if self.executions==self.fail_execution:
                if self.fault=='unchanged': result=packet
                elif self.fault=='code': result=bytes([result[0]^1])+result[1:]
                elif self.fault=='guard':
                    b=bytearray(result); b[cases.GUARD]^=1; result=bytes(b)
                elif self.fault=='nonce':
                    b=bytearray(result); b[cases.HEADER+8]^=1; result=bytes(b)
                elif self.fault=='id':
                    b=bytearray(result); b[cases.HEADER+4]^=1; result=bytes(b)
                elif self.fault=='status': self.bad_csw=(3,1)
            self.memory[address]=result
            self.queue.extend((result,self.csw(tag)))
            return len(data)
        return super().write(endpoint,data,timeout)


def minimal_elf(word=0x44004800):
    names=b'\0.text\0.shstrtab\0'
    textoff=52; stroff=56; shoff=(stroff+len(names)+3)&~3
    data=bytearray(shoff+3*40)
    ident=b'\x7fELF\x01\x01\x01'+bytes(9)
    struct.pack_into('<16sHHIIIIIHHHHHH',data,0,ident,2,92,1,cases.CODE_ADDR,0,shoff,1,52,0,0,40,3,2)
    struct.pack_into('<I',data,textoff,word)
    data[stroff:stroff+len(names)]=names
    struct.pack_into('<10I',data,shoff+40,1,1,6,cases.CODE_ADDR,textoff,4,0,0,4,0)
    struct.pack_into('<10I',data,shoff+80,7,3,0,0,stroff,len(names),0,0,1,0)
    return bytes(data)


class OracleTests(unittest.TestCase):
    def test_all_vector_inputs_are_bounded(self):
        for c in cases.CASES:
            for a in cases.vectors(c,count=32):
                self.assertLessEqual(len(a),14)
                self.assertTrue(all(0<=x<=cases.MASK for x in a))
                if c['case']==3:
                    self.assertNotEqual(a[1],0)
                    self.assertNotEqual(a,[0x80000000,cases.MASK])
                if c['case']==8: self.assertEqual(a,[0])

    def test_defined_signed_division(self):
        self.assertEqual(cases.truncdiv(-17,3),-5)
        self.assertEqual(cases.truncdiv(17,-3),-5)
        self.assertEqual(cases.truncdiv(-17,-3),5)

    def test_expected_layout_and_64bit_words(self):
        c=cases.BY_NAME['layout']; a=cases.vectors(c)[0]
        self.assertEqual(cases.expected(c,a,{}),[12,4,8,10,16,4,4,0x12,0x3456789a,0x56bcde,0xbeefa78d])
        self.assertEqual(cases.words(-1),[cases.MASK]*2)

    def test_complete_packet_checks(self):
        for name in ('echo','alu32','divide32','memory','layout','preserved','sfr_read','wide_abi','struct_abi','u64','libc_memory'):
            e=entry(name)
            for i,a in enumerate(e['vectors']):
                packet=cases.make_packet(a,123,i)
                result=make_response(e,packet)
                cases.validate_result(e,packet,result)
                for offset in (0,cases.INPUT,cases.HEADER,cases.HEADER+8,cases.GUARD):
                    broken=bytearray(result); broken[offset]^=1
                    with self.assertRaises(ValueError): cases.validate_result(e,packet,bytes(broken))

    def test_sfr_and_stack_comparisons(self):
        for name,index in (('sfr_read',1),('preserved',1),('preserved',13),('preserved',15)):
            e=entry(name); packet=cases.make_packet(e['vectors'][0],1,0)
            bad=bytearray(make_response(e,packet)); bad[cases.OUTPUT+4*index]^=1
            with self.assertRaises(ValueError): cases.validate_result(e,packet,bytes(bad))

    def test_float_oracle_signed_zero(self):
        self.assertEqual(cases.fbits(-0.0),0x80000000)
        e=entry('float_mul'); c=cases.BY_NAME['float_mul']
        self.assertEqual(cases.expected(c,[cases.fbits(-0.0),cases.fbits(2.0)],e),[0x80000000])

    def test_packet_rejects_excess_inputs(self):
        e=entry()
        for a in ([1]*15,[-1],[1<<32]):
            with self.assertRaises(ValueError): cases.make_packet(a,1,0)


class ElfTests(unittest.TestCase):
    def test_valid_minimal_elf(self):
        code,symbols=build.elf_info(minimal_elf(),cases.CODE_ADDR,cases.CODE_LIMIT)
        self.assertEqual(code,bytes.fromhex('00480044'))

    def test_wrong_endian_delay_entry_truncation(self):
        for offset,value in ((5,2),(36,0),(24,1)):
            data=bytearray(minimal_elf()); data[offset]=value
            with self.assertRaises(ValueError): build.elf_info(bytes(data),cases.CODE_ADDR,cases.CODE_LIMIT)
        with self.assertRaises(ValueError): build.elf_info(minimal_elf()[:60],cases.CODE_ADDR,cases.CODE_LIMIT)

    def test_forbidden_spr_writes_and_unapproved_reads(self):
        for word in (0xc0000000,0x20000000,0x24000000,0xb4000000):
            with self.assertRaises(ValueError): build.elf_info(minimal_elf(word),cases.CODE_ADDR,cases.CODE_LIMIT)
        build.elf_info(minimal_elf(0xb4000000),cases.CODE_ADDR,cases.CODE_LIMIT,spr_reads={0})

    def test_spr_allowlist_encoding_and_static_base(self):
        # l.mtspr r0,r15,0x18 writes SPR 0x18 (SR) with a static r0 base.
        build.elf_info(minimal_elf(0xc0007818),cases.CODE_ADDR,cases.CODE_LIMIT,spr_writes={0x18})
        with self.assertRaises(ValueError): build.elf_info(minimal_elf(0xc0007818),cases.CODE_ADDR,cases.CODE_LIMIT)
        # l.mfspr r15,r0,0x18 reads SPR 0x18; a statically encoded read must be listed.
        build.elf_info(minimal_elf(0xb5e00018),cases.CODE_ADDR,cases.CODE_LIMIT,spr_reads={0x18})
        with self.assertRaises(ValueError): build.elf_info(minimal_elf(0xb5e00018),cases.CODE_ADDR,cases.CODE_LIMIT,spr_reads={0})
        # A non-r0 base is never allowed for writes, because the address is then
        # not statically known and a wrong write can disturb the running firmware.
        with self.assertRaises(ValueError):
            build.elf_info(minimal_elf(0xc0032000),cases.CODE_ADDR,cases.CODE_LIMIT,spr_writes={0x2000})
        # Register-base reads are permitted: the front end emits them for constant
        # __sfr addresses at -O0 and mfspr has no side effects.
        build.elf_info(minimal_elf(0xb4830000),cases.CODE_ADDR,cases.CODE_LIMIT)


class TransportTests(unittest.TestCase):
    def test_unknown_firmware_no_ram_writes(self):
        camera=TestCamera(); camera.memory[dump.ROUTINE_SIGNATURES[0][0]]=bytes(16)
        with TestTransport(fake_usb(camera),camera,100) as dev:
            with self.assertRaises(dump.DumpError): dev.check_firmware()
            with self.assertRaises(dump.DumpError): dev.upload(dump.CANARY_ADDR,dump.CANARY_BYTES)
        self.assertFalse(any(c[2]==0 for c in camera.commands))
        self.assertIn(('attach',4),camera.actions)

    def test_profile_requires_canary(self):
        camera=TestCamera()
        with TestTransport(fake_usb(camera),camera,100) as dev:
            with self.assertRaises(dump.DumpError): dev.set_profile(entry(),bytes(2048))

    def test_readback_failure_stops_before_execution(self):
        camera=TestCamera(); camera.corrupt_upload=True
        with TestTransport(fake_usb(camera),camera,100) as dev:
            dev.check_firmware()
            with self.assertRaises(dump.DumpError): dev.baseline()
        self.assertFalse(any(c[3] in (dump.CANARY_ADDR,dump.SHIM_ADDR) for c in camera.commands))

    def test_transaction_policy_and_verified_execution(self):
        e=entry(); camera=TestCamera([e]); usb=fake_usb(camera)
        original=dict(dump.RAM_BLOBS)
        with TestTransport(usb,camera,100) as dev:
            dev.check_firmware(); dev.baseline()
            packet=cases.make_packet(e['vectors'][0],1,0)
            dev.set_profile(e,packet)
            bad_calls=[lambda:dev._command(cases.BASE,4096),
                       lambda:dev._command(0x02000000,4),
                       lambda:dev._command(cases.BASE,2048,0x02000000),
                       lambda:dev._command(cases.BASE,2048,dump.SHIM_ADDR,1),
                       lambda:dev._command(cases.BASE,2048,dump.CACHE_CLEAN_FN,payload=bytes(2048)),
                       lambda:dev._command(cases.BASE,2048,dump.SHIM_ADDR)]
            for call in bad_calls:
                with self.assertRaises(dump.DumpError): call()
            dev.upload(dump.SHIM_ADDR,cases.DISPATCH); dev.upload(cases.CODE_ADDR,e['blob']); dev.upload(cases.BASE,packet)
            with self.assertRaises(dump.DumpError): dev._command(cases.BASE,2048,dump.SHIM_ADDR)
            dev.invalidate_code()
            got=dev._command(cases.BASE,2048,dump.SHIM_ADDR)
            cases.validate_result(e,packet,got)
            with self.assertRaises(dump.DumpError): dev._command(cases.BASE,2048,dump.SHIM_ADDR)
        self.assertEqual(dump.RAM_BLOBS,original)

    def test_full_icache_signature_before_ram_writes(self):
        camera=TestCamera()
        bad=bytearray(ICACHE_SIGNATURE); bad[-4]^=1
        camera.memory[ICACHE_INIT_FN]=bytes(bad)
        with TestTransport(fake_usb(camera),camera,100) as dev:
            with self.assertRaises(dump.DumpError): dev.check_firmware()
        self.assertFalse(any(c[2]==0 for c in camera.commands))

    def test_icache_callback_only_after_verified_upload(self):
        camera=TestCamera()
        with TestTransport(fake_usb(camera),camera,100) as dev:
            dev.check_firmware()
            with self.assertRaises(dump.DumpError): dev.invalidate_code()
            dev.baseline()
            for address,length,arg in ((cases.BASE,4096,0),(dump.SHIM_ADDR,4,0),(cases.BASE,2048,1)):
                with self.assertRaises(dump.DumpError): dev._command(address,length,ICACHE_INIT_FN,arg)

    def test_packet_profile_not_arbitrary(self):
        e=entry(); camera=TestCamera([e])
        with TestTransport(fake_usb(camera),camera,100) as dev:
            dev.check_firmware(); dev.baseline()
            packet=bytearray(cases.make_packet(e['vectors'][0],1,0)); packet[0]^=1
            with self.assertRaises(dump.DumpError): dev.set_profile(e,bytes(packet))


class RunnerTests(unittest.TestCase):
    def suite(self):
        entries=[entry(),entry('sfr_read',65)]
        return dict(compiler='fake',sources={},runtime_hashes={},seed=3292,random_count=0,
                    layout=dict(code=cases.CODE_ADDR,code_limit=cases.CODE_LIMIT,
                                packet=cases.BASE,size=cases.SIZE),
                    tests=entries,excluded=[])

    def test_complete_fake_run_and_evidence(self):
        s=self.suite(); camera=TestCamera(s['tests']); camera.fragment=29
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/'run'
            with TestTransport(fake_usb(camera),camera,100) as dev,contextlib.redirect_stdout(io.StringIO()):
                result=runner.run_suite(s,dev,camera.data,output)
            self.assertEqual(result['status'],'pass')
            self.assertEqual(result['passed_vectors'],4)  # baseline SPI x2, echo, SPR
            self.assertEqual(result['spr_vr'],0x87654321)
            self.assertEqual(json.loads((output/'run.json').read_text())['status'],'pass')
            self.assertEqual(len(list(output.glob('*.bin'))),5)
            self.assertIn(('release',4),camera.actions)

    def test_faults_stop_and_save_partial_evidence(self):
        for fault in ('unchanged','code','guard','nonce','id','status'):
            with self.subTest(fault=fault),tempfile.TemporaryDirectory() as folder:
                s=self.suite(); camera=TestCamera(s['tests']); camera.fault=fault
                output=Path(folder)/'run'
                with TestTransport(fake_usb(camera),camera,100) as dev,contextlib.redirect_stdout(io.StringIO()):
                    with self.assertRaises((ValueError,dump.DumpError)):
                        runner.run_suite(s,dev,camera.data,output)
                self.assertEqual(camera.executions,1)
                report=json.loads((output/'run.json').read_text())
                self.assertEqual(report['status'],'fail')
                self.assertEqual(report['passed_vectors'],2)
                self.assertEqual(report['results'][-1]['status'],'fail')
                self.assertTrue((output/'last-transfer.bin').exists())

    def test_skipping_real_icache_callback_reproduces_stale_spi(self):
        s=self.suite(); camera=TestCamera(s['tests'])
        with tempfile.TemporaryDirectory() as folder,TestTransport(fake_usb(camera),camera,100) as dev,contextlib.redirect_stdout(io.StringIO()):
            # Fault injection only: lie about instruction visibility without a USB flush.
            dev.invalidate_code=lambda: setattr(dev,'icache_ready',True)
            with self.assertRaises(ValueError): runner.run_suite(s,dev,camera.data,Path(folder)/'run')
            report=json.loads((Path(folder)/'run/run.json').read_text())
            row=report['results'][-1]
            received=(Path(folder)/'run'/row['raw_file']).read_bytes()
            self.assertEqual(received,camera.data[:2048])
            self.assertEqual(camera.executions,0)

    def test_selected_smoke_scope(self):
        s=self.suite(); camera=TestCamera(s['tests'])
        with tempfile.TemporaryDirectory() as folder,TestTransport(fake_usb(camera),camera,100) as dev,contextlib.redirect_stdout(io.StringIO()):
            report=runner.run_suite(s,dev,camera.data,Path(folder)/'run',only=('echo-Os',))
        self.assertEqual(report['scope'],'selected')
        self.assertEqual(report['passed_vectors'],3)
        self.assertEqual(report['not_selected'],['sfr_read-Os'])
        self.assertEqual(camera.executions,1)

    def test_invalid_reference_before_commands(self):
        s=self.suite(); camera=TestCamera(s['tests'])
        with tempfile.TemporaryDirectory() as folder,TestTransport(fake_usb(camera),camera,100) as dev:
            with self.assertRaises((ValueError,dump.DumpError)):
                runner.run_suite(s,dev,image(),Path(folder)/'run')
        self.assertEqual(camera.commands,[])

    def test_existing_output_never_overwritten(self):
        s=self.suite(); camera=TestCamera(s['tests'])
        with tempfile.TemporaryDirectory() as folder,TestTransport(fake_usb(camera),camera,100) as dev:
            with self.assertRaises(FileExistsError): runner.run_suite(s,dev,camera.data,Path(folder))
        self.assertEqual(camera.commands,[])

    def test_blocked_profiles_never_executed(self):
        s=self.suite(); s['tests'].append(dict(key='unsafe',status='blocked_safety',reason='SPR write'))
        camera=TestCamera(s['tests'][:2])
        with tempfile.TemporaryDirectory() as folder,TestTransport(fake_usb(camera),camera,100) as dev,contextlib.redirect_stdout(io.StringIO()):
            report=runner.run_suite(s,dev,camera.data,Path(folder)/'run')
        self.assertEqual(report['status'],'incomplete')
        self.assertEqual(report['not_run'],[dict(key='unsafe',reason='SPR write')])

    def test_help_without_usb(self):
        with patch.object(dump,'load_usb',side_effect=AssertionError('USB must not load')):
            with contextlib.redirect_stdout(io.StringIO()),self.assertRaises(SystemExit) as exc:
                runner.main(['--help'])
        self.assertEqual(exc.exception.code,0)


if __name__=='__main__': unittest.main()
