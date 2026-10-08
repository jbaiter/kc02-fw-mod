"""Optional compiled-suite integration, still entirely offline.

OR1K_TEST_SUITE=patched/toolchain-tests/build python3 -m unittest discover -s tests -v

The fake camera executes RAM payload machine code in the bounded model. Stock
SPI/cache routines remain simulated: this is not vendor-ABI or device evidence.
"""
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch

from test_toolchain_runner import TestCamera, fake_usb, runner, dump
from toolchain_tests import build, cases, model
from toolchain_tests.transport import TestTransport

SUITE=os.environ.get('OR1K_TEST_SUITE')


class ModelCamera(TestCamera):
    def __init__(self, entries):
        super().__init__(e for e in entries if e['status']=='ready' and e['kind']=='ram')
        self.spi={e['blob']:e for e in entries if e['status']=='ready' and e['kind']=='spi'}

    def write(self,endpoint,data,timeout):
        if not self.pending:
            signature,tag,length,direction,lun,cdb_len=struct.unpack('<4sIIBBB',data[:15])
            opcode,function,address,callback=struct.unpack('<BIII',data[15:28])
            offset=int.from_bytes(data[28:31],'little')
            shim=self.memory.get(dump.SHIM_ADDR)
            if direction==0x80 and callback==dump.SHIM_ADDR and shim!=dump.SHIM_BYTES:
                assert opcode==0xcd and function==dump.MEMORY_TRANSFER_FN
                self.commands.append((address,length,direction,callback,offset))
                packet=self.memory[address]
                if shim==cases.DISPATCH:
                    entry=self.entries[self.read_code()]
                    result,_=model.execute(entry,packet)
                    self.executions+=1
                else:
                    entry=self.spi[shim]
                    _,calls=model.execute(entry,packet,offset)
                    assert calls==[(offset,cases.BASE,cases.SIZE)]
                    result=self.data[offset:offset+length]
                self.memory[address]=result
                self.queue.extend((result,self.csw(tag)))
                return len(data)
        return super().write(endpoint,data,timeout)


@unittest.skipUnless(SUITE,'set OR1K_TEST_SUITE to test a built suite offline')
class CompiledSuiteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.suite=build.load_suite(SUITE)

    def test_entire_compiled_suite_through_fake_usb(self):
        camera=ModelCamera(self.suite['tests']); camera.fragment=61
        with tempfile.TemporaryDirectory() as directory:
            with TestTransport(fake_usb(camera),camera,100) as dev,contextlib.redirect_stdout(io.StringIO()):
                report=runner.run_suite(self.suite,dev,camera.data,Path(directory)/'evidence')
            expected=model.check_model(self.suite)+2  # original assembly SPI baseline
            self.assertEqual(report['passed_vectors'],expected)
            self.assertEqual(report['status'],'incomplete' if report['not_run'] else 'pass')
            self.assertIn(('attach',4),camera.actions)
            self.assertTrue((Path(directory)/'evidence/suite.json').exists())

    def test_branch_oracle_detects_each_inverted_branch(self):
        e=next(e for e in self.suite['tests'] if e['status']=='ready' and e['name']=='isa_calls')
        packet=cases.make_packet(e['vectors'][0],123,0)
        branches=[]
        for offset in range(0,e['code_size'],4):
            word=struct.unpack_from('<I',e['blob'],offset)[0]
            if word>>26 in (3,4): branches.append((offset,word))
        self.assertEqual(len(branches),4)
        for offset,word in branches:
            altered=bytearray(e['blob'])
            opposite=4 if word>>26==3 else 3
            struct.pack_into('<I',altered,offset,(word&0x3ffffff)|(opposite<<26))
            received,_=model.execute(dict(e,blob=bytes(altered)),packet)
            with self.assertRaises(ValueError): cases.validate_result(e,packet,received)

    def test_altered_binary_refused_before_usb(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'suite'; shutil.copytree(SUITE,root)
            e=next(e for e in self.suite['tests'] if e['status']=='ready')
            p=root/e['files']['payload.bin']['path']
            data=bytearray(p.read_bytes()); data[0]^=1; p.write_bytes(data)
            with patch.object(dump,'load_usb',side_effect=AssertionError('USB must not load')):
                with contextlib.redirect_stderr(io.StringIO()):
                    status=runner.main(['run','--suite',str(root),'--ack-ram-execution'])
            self.assertEqual(status,1)

    def test_missing_profile_and_source_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'suite'; shutil.copytree(SUITE,root)
            p=root/'manifest.json'; manifest=json.loads(p.read_text())
            manifest['tests'].pop(); p.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'matrix'): build.load_suite(root)
            manifest['sources']={}; p.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'source hashes'): build.load_suite(root)

    def test_acknowledgement_required_before_usb(self):
        with patch.object(dump,'load_usb',side_effect=AssertionError('USB must not load')):
            with contextlib.redirect_stderr(io.StringIO()):
                status=runner.main(['run','--suite',SUITE])
        self.assertEqual(status,1)


if __name__=='__main__': unittest.main()
