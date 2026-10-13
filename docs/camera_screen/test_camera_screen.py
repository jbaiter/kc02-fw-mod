#!/usr/bin/env python3
"""Host mock tests + bounded differential instruction checks of stock handlers.
Not an emulator of the SoC, and not proof of target ABI/hardware equivalence.
"""
import argparse
import ctypes as C
import hashlib
import json
import re
import subprocess
from pathlib import Path
from build_camera_screen import imports
from generate_widgets import generate,SHA256
from generate_mock_wrappers import generate as generate_mocks
HERE=Path(__file__).resolve().parent

class OriginalHandler:
    """Execute the selected original no-delay OR1K instruction listings.
    Direct callees are stubs: only XOSTimeGet has a nonzero return value.
    Compare controller-owned RAM, not the behavior of called firmware drivers.
    """
    def __init__(self,text,address,image):
        region=text.split(f'=== 0x{address:08x} ',1)[1].split('\n=== ',1)[0]
        self.code={int(pc,16):(op,args) for pc,op,args in re.findall(r'ram:([0-9a-f]{8})\s+l\.(\w+)\s*(.*)',region)}
        self.address=address
        assert self.code,'empty original function'
        # Re-encode each listing instruction and compare it to the pinned image.
        # This prevents an edited or wrongly decoded listing becoming a golden model.
        reg=lambda s:int(s[1:])
        immediate={'addi':0x27,'andi':0x29,'ori':0x2a,'slli':0x2e}
        compare={'sfeqi':0,'sfnei':1,'sfgtui':2,'sfleui':5}
        load={'lwz':0x21,'lbz':0x23,'lbs':0x24}
        for pc,(op,text) in self.code.items():
            a=text.replace(' ','').split(',')
            if op=='movhi':word=(6<<26)|(reg(a[0])<<21)|(int(a[1],0)&0xffff)
            elif op in immediate:word=(immediate[op]<<26)|(reg(a[0])<<21)|(reg(a[1])<<16)|(int(a[2],0)&0xffff)
            elif op=='add':word=(0x38<<26)|(reg(a[0])<<21)|(reg(a[1])<<16)|(reg(a[2])<<11)
            elif op in compare:word=(0x2f<<26)|(compare[op]<<21)|(reg(a[0])<<16)|(int(a[1],0)&0xffff)
            elif op in ['j','jal','bf','bnf']:
                opcode={'j':0,'jal':1,'bf':4,'bnf':3}[op]
                word=(opcode<<26)|(((int(a[0],0)-pc)//4)&0x3ffffff)
            elif op=='jr':word=(0x11<<26)|(reg(a[0])<<11)
            elif op in load or op in ['sb','sw']:
                address_arg=a[1] if op in load else a[0]
                m=re.fullmatch(r'(-?0x[0-9a-f]+|-?\d+)\(r(\d+)\)',address_arg);assert m
                imm=int(m[1],0)&0xffff;base=int(m[2])
                if op in load:word=(load[op]<<26)|(reg(a[0])<<21)|(base<<16)|imm
                else:word=({'sw':0x35,'sb':0x36}[op]<<26)|((imm>>11)<<21)|(base<<16)|(reg(a[1])<<11)|(imm&0x7ff)
            else:raise AssertionError('unsupported instruction '+op)
            offset=pc-0x01ffda00
            actual=int.from_bytes(image[offset:offset+4],'little')
            assert word==actual,f'listing does not match firmware at {pc:08x}: {op} {text}; {word:08x} != {actual:08x}'
    def run(self,mem,flags,event,clock=1234):
        registers=[0]*32;r=registers
        r[1]=0x04010000;r[3]=0x1001;r[4]=event;r[5]=0x04011000;r[9]=0xffffffff
        stack={};flag=False;pc=self.address
        def read(a,n,signed=False):
            b=bytes((mem if 0x02000000<=a+i<0x02400000 else stack).get(a+i,0) for i in range(n))
            return int.from_bytes(b,'little',signed=signed)&0xffffffff
        def write(a,v,n):
            b=(v&((1<<(8*n))-1)).to_bytes(n,'little')
            for i,x in enumerate(b): (mem if 0x02000000<=a+i<0x02400000 else stack)[a+i]=x
        write(r[5],flags,4)
        def val(s): return r[int(s[1:])] if s.startswith('r') else int(s,0)
        def dest(s,v):
            i=int(s[1:])
            if i:r[i]=v&0xffffffff
        def addr(s):
            m=re.fullmatch(r'(-?0x[0-9a-f]+|-?\d+)\(r(\d+)\)',s);assert m,s
            return (int(m[1],0)+r[int(m[2])])&0xffffffff
        for _ in range(10000):
            if pc==0xffffffff:return mem
            op,text=self.code[pc];a=text.replace(' ','').split(',');next_pc=pc+4
            if op=='movhi':dest(a[0],val(a[1])<<16)
            elif op in ('ori','andi','addi','slli'):
                x,y=val(a[1]),val(a[2]);v={'ori':lambda:x|y,'andi':lambda:x&y,'addi':lambda:x+y,'slli':lambda:x<<y}[op]()
                dest(a[0],v)
            elif op=='add':dest(a[0],val(a[1])+val(a[2]))
            elif op in ('lbz','lbs','lwz'):
                dest(a[0],read(addr(a[1]),4 if op=='lwz' else 1,op=='lbs'))
            elif op in ('sb','sw'):write(addr(a[0]),val(a[1]),4 if op=='sw' else 1)
            elif op in ('sfeqi','sfnei','sfgtui','sfleui'):
                x,y=val(a[0]),val(a[1])&0xffffffff
                flag={'sfeqi':lambda:x==y,'sfnei':lambda:x!=y,'sfgtui':lambda:x>y,'sfleui':lambda:x<=y}[op]()
            elif op=='bf':
                if flag:next_pc=val(a[0])
            elif op=='bnf':
                if not flag:next_pc=val(a[0])
            elif op=='j':next_pc=val(a[0])
            elif op=='jal':r[9]=pc+4;r[11]=clock if val(a[0])==0x02021894 else 0
            elif op=='jr':next_pc=val(a[0])
            else:raise AssertionError('unsupported instruction '+op)
            pc=next_pc
        raise AssertionError('instruction limit')

class Harness:
    def __init__(self,args):
        args.out.mkdir(parents=True,exist_ok=True)
        generate(args.firmware,args.out/'camera_widgets.inc');generate_mocks(args.out)
        so=args.out/'camera_host_test.so'
        command=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-DCAMERA_HOST_TEST','-fPIC','-shared','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(args.out),'-I'+str(HERE),str(HERE/'camera_screen_model.c'),str(HERE/'mock_firmware.c'),'-o',str(so)]
        subprocess.run(command,check=True)
        self.lib=C.CDLL(str(so));self.ids={r['name']:i for i,r in enumerate(imports())};self.tests=0
        self.lib.camera_test_memory.argtypes=[C.c_uint32,C.c_size_t];self.lib.camera_test_memory.restype=C.c_void_p
        self.lib.mock_set.argtypes=[C.c_uint32,C.c_size_t]
        self.lib.mock_arg.restype=C.c_size_t;self.lib.mock_resource.restype=C.c_size_t
        self.lib.mock_text.restype=C.c_char_p
        self.image=args.firmware.read_bytes();assert hashlib.sha256(self.image).hexdigest()==SHA256
        self.command=command
    def reset(self):
        self.lib.mock_reset()
        # Restore only resource-table/text bytes from flash, not controller RAM.
        C.memmove(self.ptr(0x020c2550,0x80),self.image[0x020c2550-0x01ffda00:0x020c25d0-0x01ffda00],0x80)
        C.memmove(self.ptr(0x0207b800,0x400),self.image[0x0207b800-0x01ffda00:0x0207bc00-0x01ffda00],0x400)
    def ptr(self,a,n=1):return self.lib.camera_test_memory(a,n)
    def set8(self,a,v):C.c_uint8.from_address(self.ptr(a)).value=v
    def get8(self,a):return C.c_uint8.from_address(self.ptr(a)).value
    def set32(self,a,v):C.c_uint32.from_address(self.ptr(a,4)).value=v
    def get32(self,a):return C.c_uint32.from_address(self.ptr(a,4)).value
    def app(self,off,value=None):
        a=0x020d416c+off
        if value is not None:self.set8(a,value)
        return self.get8(a)
    def configure(self,name,value):self.lib.mock_set(self.ids[name],value)
    def calls(self,name):return self.lib.mock_calls(self.ids[name])
    def arg(self,name,arg):return self.lib.mock_arg(self.ids[name],arg)
    def event(self,name,flag=0,event=1):
        p=C.c_uint32(flag);f=getattr(self.lib,name);f.argtypes=[C.c_size_t,C.c_uint32,C.POINTER(C.c_uint32)]
        return f(0x1001,event,C.byref(p))
    def check(self,condition,message):
        assert condition,message;self.tests+=1

    def logic_tests(self):
        self.reset();self.lib.camera_mode_enter()
        self.check(self.app(0x58)==1 and self.get32(0x020ccd8c)==1,'mode enter flags')
        self.check(self.calls('fw_window_open')==1 and self.calls('fw_glyphs_init')==1,'enter owns widget lifecycle')
        self.check(self.arg('fw_window_open',0)!=0x020c2b40,'uses replacement descriptor')
        self.check(self.lib.mock_visibility(0x18)==1 and self.lib.mock_text(0x18)==b'000000','remaining counter')
        self.event('camera_window_exit');self.check(self.get8(0x020c8780)==0 and self.calls('fw_glyphs_free')==1,'window exit')
        self.lib.camera_mode_exit();self.check(self.app(0x58)==0 and self.app(0x5c)==1,'exit no-frame handshake')
        for group,effect in [(0,None),(2,'fw_effect'),(3,'fw_group3_effect')]:
            self.reset();self.lib.mock_frame(1);self.app(0x59,group);self.lib.camera_mode_tick()
            self.check(self.app(0x5c)==2 and self.calls('fw_frame_submit')==1 and self.calls('fw_frame_rearm')==1,'preview submit/rearm')
            self.check(self.calls('fw_effect')==(group==2) and self.calls('fw_group3_effect')==(group==3),'preview effect selection')
        self.reset();self.lib.camera_mode_tick();self.check(self.app(0x5c)==1 and not self.calls('fw_frame_submit'),'null preview frame')
        for option,period in [(0,31),(1,24)]:
            for direction in ['camera_down','camera_up']:
                self.reset();self.app(0x5f,option)
                for _ in range(period):self.event(direction,2)
                self.check(self.app(0x59)==0 and self.app(0x5b)==0,'complete forward/backward preset cycle')
        self.reset();self.app(8,14);self.event('camera_up',1);self.event('camera_up',1)
        self.check(self.app(8)==15 and self.lib.mock_text(0x16)==b'16X','zoom upper clamp')
        self.event('camera_down',2);self.check(self.app(0x6d)==2 and self.app(0x5b)==0,'first release ends zoom, not preset')
        self.lib.mock_clock(3000,0);self.lib.camera_indicator_timeout(0x1001)
        self.check(self.app(0x6d)==0 and self.lib.mock_visibility(0x17)==0,'zoom indicator expiry')
        self.reset();self.event('camera_left');self.check(self.app(0x5f)==1,'print option toggle')
        self.event('camera_left',1);self.check(self.app(0x5f)==1,'flag guard')
        self.event('camera_right');self.event('camera_right');self.check(self.get32(0x020ccd98)==2 and self.arg('fw_board_ioctl',2)==1,'light enable')
        self.event('camera_right');self.check(self.get32(0x020ccd98)==0 and self.arg('fw_board_ioctl',2)==0,'light disable')
        self.event('camera_back');self.check(self.arg('fw_mode_request',0)==6 and self.app(0x68)==2,'back to main menu')
        self.reset();self.app(4,0);self.event('camera_msg_storage')
        self.check(self.arg('fw_window_open',1)==2 and self.arg('fw_window_open',2)==0x81000059 and self.arg('fw_window_open',3)==2,'variadic popup arguments')
        for level,icon in [(0,0x80000021),(3,0x80000024),(5,0x80000025)]:
            self.reset();self.app(0,level);self.event('camera_msg_battery')
            self.check(self.lib.mock_resource(0xa)==icon,'battery icon mapping')
        self.reset();self.app(2,1);self.event('camera_msg_clock')
        self.check(self.lib.mock_text(0xc)==b'2026/01/02 03:04:05' and self.app(0x6a)==1,'RTC and charging icon tick')
        self.reset();self.app(4,4);self.set32(0x020d41b8,120*123)
        self.lib.camera_refresh_remaining(0x1001);self.check(self.lib.mock_text(0x18)==b'000123','capacity estimate')
        self.reset();self.event('camera_ok');self.check(self.calls('fw_queue_post')==1,'no SD and no print posts storage')
        for preset,shots in [(0,1),(1,3),(2,5),(3,1),(4,1),(5,1),(6,3)]:
            self.reset();self.app(4,4);self.app(0x5b,preset);self.lib.mock_clock(1234,1000)
            self.event('camera_ok');self.check(self.calls('fw_encode_file')==shots,'capture/burst/countdown count')
            self.check(self.calls('fw_file_add')==shots and self.calls('fw_sd_unlock')==shots,'file accounting/lock release')
            self.check(self.arg('fw_encode_file',8)==0x294,'stack-passed encode argument')
        self.reset();self.app(4,4);self.app(0x5f,1);self.configure('fw_manager_service',1);self.configure('fw_print_file',8)
        self.event('camera_ok');self.check(self.calls('fw_print_file')==1 and self.calls('fw_close')==3,'saved file printing and closes')
        self.check(self.lib.mock_resource(0x15)==0x81000057 and self.lib.mock_visibility(0x13)==0,'no-paper status then hide')
        self.reset();self.app(4,4);self.configure('fw_space_check',0xffffffff)
        self.event('camera_ok');self.check(self.app(4)==3 and not self.calls('fw_encode_file') and self.calls('fw_sd_unlock')==1,'full-card path')
        self.reset();self.app(4,4);self.configure('fw_encode_file',0xffffffff)
        self.event('camera_ok');self.check(self.calls('fw_unlink')==1 and self.calls('fw_file_add')==0,'failed capture cleanup')
        self.reset();self.app(0x5f,1);self.event('camera_ok')
        self.check(self.calls('fw_encoded_get')==2 and self.calls('fw_print_image')==1 and self.lib.mock_allocations()==0,'RAM print encode and ownership')
        self.check(self.arg('fw_encode_start',5)==1,'second-stage sixth encode control')
        self.reset();self.app(0x5f,1);self.configure('fw_alloc',0);self.event('camera_ok')
        self.check(self.calls('fw_print_image')==0 and self.calls('fw_encoder_uninit')==1,'allocation failure cleanup')
        self.reset();self.app(0x5f,1);self.configure('fw_encoded_get',0);self.lib.mock_clock(0,1000);self.event('camera_ok')
        self.check(self.calls('fw_print_image')==0 and self.lib.mock_allocations()==0,'stream timeout cleanup')
        self.reset();self.app(0x5f,1);self.configure('fw_jpeg_decode',0xffffffff);self.event('camera_ok')
        self.check(self.calls('fw_print_image')==0 and self.lib.mock_allocations()==0,'decode failure cleanup')

    def differential_tests(self,listing):
        text=listing.read_text()
        cases=[]
        states=[(0,p,0,0,0) for p in range(9)]+[(1,0,p,0,0) for p in range(1,6)]+[(2,0,0,p,0) for p in range(8)]+[(3,0,0,0,p) for p in range(9)]
        for name,address in [('camera_up',0x02009890),('camera_down',0x02009568)]:
            em=OriginalHandler(text,address,self.image)
            for state in states:
                for option in [0,1]:
                    for zoom in [0,1,14,15]:
                        for flag in [0,1,2]:
                            cases.append((name,em,state,option,zoom,flag,1,0,0))
            for indicator in [1,2]:
                cases.append((name,em,(0,0,0,0,0),0,5,2,1,indicator,0))
        for name,address in [('camera_left',0x02009de0),('camera_right',0x02009f6c),('camera_back',0x02009f08)]:
            em=OriginalHandler(text,address,self.image)
            for state in states:
                for option in [0,1]:
                    for flag in [0,1,2]:
                        for event in [0,1]:
                            for light in range(4):cases.append((name,em,state,option,0,flag,event,0,light))
        watched=list(range(0x020d416c,0x020d416c+0x74))+list(range(0x020ccd98,0x020ccd9c))+list(range(0x020ccda4,0x020ccda8))
        for name,em,state,option,zoom,flag,event,indicator,light in cases:
            self.reset();self.lib.mock_clock(1234,0)
            for off,v in zip([0x59,0x5b,0x5a,0x5d,0x5e],state):self.app(off,v)
            self.app(0x5f,option);self.app(8,zoom);self.app(0x6d,indicator);self.set32(0x020ccd98,light)
            mem={a:self.get8(a) for a in watched}
            for i in range(0x80):mem[0x020c2550+i]=self.get8(0x020c2550+i)
            expected=em.run(mem.copy(),flag,event)
            self.event(name,flag,event)
            actual=bytes(self.get8(a) for a in watched);want=bytes(expected.get(a,0) for a in watched)
            self.check(actual==want,f'differential {name} state={state} print={option} zoom={zoom} flags={flag} event={event} indicator={indicator} light={light}; differences '+str([(hex(a),x,y) for a,x,y in zip(watched,actual,want) if x!=y]))
        return len(cases)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--firmware',type=Path,required=True);ap.add_argument('--listing',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args();h=Harness(args);h.logic_tests();differential=h.differential_tests(args.listing)
    result=dict(passed=True,assertions=h.tests,differential_cases=differential,original_handlers=['UP','DOWN','LEFT','RIGHT','BACK'],
        firmware_sha256=SHA256,original_listing_sha256=hashlib.sha256(args.listing.read_bytes()).hexdigest(),original_listing_byte_checked=True,
        sanitizers=['undefined-behavior sanitizer'],build_command=h.command,
        source_sha256={name:hashlib.sha256((HERE/name).read_bytes()).hexdigest() for name in ['camera_screen_model.c','camera_screen.h','firmware_imports.def','generate_widgets.py','generate_mock_wrappers.py','mock_firmware.c','test_camera_screen.py']},
        limitations=['Original-handler callees are mocked; controller RAM is compared, not peripheral behavior.','Not target ABI, preview hardware, JPEG quality, printer or deployment validation.'])
    (args.out/'test_results.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'PASS: {h.tests} assertions, including {differential} differential controller-state cases against five original handlers.')
if __name__=='__main__':main()
