#!/usr/bin/env python3
"""Exercise actual Luma guest code through native ADC keys on all three boards."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time
from run import command, ROOT
from card import create

class QMP:
    def __init__(self,path):
        self.sock=socket.socket(socket.AF_UNIX)
        self.sock.settimeout(5)
        self.sock.connect(str(path))
        self.stream=self.sock.makefile('rwb')
        assert 'QMP' in json.loads(self.stream.readline())
        self.call('qmp_capabilities')
    def call(self,name,**args):
        self.stream.write(json.dumps(dict(execute=name,arguments=args)).encode()+b'\n')
        self.stream.flush()
        while True:
            r=json.loads(self.stream.readline())
            if 'error' in r:
                raise RuntimeError(r)
            if 'return' in r:
                return r['return']
    def key(self,key,hold=.18):
        for down in (True,False):
            self.call('input-send-event',events=[dict(type='key',data=dict(down=down,key=dict(type='qcode',data=key)))])
            time.sleep(hold if down else .12)
    def close(self):
        self.stream.close()
        self.sock.close()

def wait(proc,predicate,label,seconds=12):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if proc.poll() is not None:
            raise RuntimeError(f'QEMU exited {proc.returncode}: {label}')
        if predicate():
            return
        time.sleep(.03)
    raise TimeoutError(label)

def exercise(board,build,qemu,output,nm,theme):
    directory=output/board
    directory.mkdir()
    card=create(directory/'card.img',theme=theme,demo=True)
    symbols={parts[2]:int(parts[0],16) for line in subprocess.check_output(
        [nm,'--defined-only',str(build/board/'luma.elf')],text=True).splitlines()
        if len(parts:=line.split())==3}
    captures={}
    for boot in (1,2):
        sock=directory/'qmp.sock'
        sock.unlink(missing_ok=True)
        cmd=command(board,build,qemu,sd=card,headless=True,qmp=f'unix:{sock},server=on,wait=off')
        env={k:v for k,v in os.environ.items() if not k.startswith('AX3292_')}
        with (directory/f'qemu-{boot}.log').open('wb') as log:
            proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=log)
            q=None
            try:
                wait(proc,sock.exists,'QMP socket')
                q=QMP(sock)
                def value(name,offset=0):
                    target=directory/'observation.bin'
                    q.call('pmemsave',val=symbols[name]+offset,size=4,filename=str(target))
                    return int.from_bytes(target.read_bytes(),'little')
                def state(offset):
                    return value('luma_app',offset)
                def save(name):
                    # State transitions precede DE scanout. Observe rendered
                    # frames, then allow the main-thread display BH to publish.
                    previous=value('luma_frames')
                    wait(proc,lambda:value('luma_frames')>=previous+2,'fresh scanout')
                    time.sleep(.06)
                    path=directory/f'{name}.ppm'
                    q.call('screendump',filename=str(path))
                    captures[name]=hashlib.sha256(path.read_bytes()).hexdigest()
                    return path.read_bytes()
                wait(proc,lambda:value('luma_frames')>=2,'rendered boot animation')
                if boot==1 and state(32):
                    save('01-boot')
                wait(proc,lambda:state(32)==0 and value('luma_frames')>6,'camera after boot')
                assert state(0)==0
                assert state(16)==(2 if boot==1 else 3)
                # Verify SD theme data reached guest-owned buffers, not merely
                # that the test card retained its files after the run.
                import re
                config=(theme/'THEME.INI').read_text()
                color=re.search(r'^background\s*=\s*#([0-9a-fA-F]{6})\s*$',config,re.M)
                rgb=int(color[1],16) if color else 0x0c1218
                expected=((rgb>>16)&248)<<8 | ((rgb>>8)&252)<<3 | (rgb&255)>>3
                assert value('luma_app',84)&0xffff==expected
                from PIL import Image
                for key,symbol in (('boot_image','theme_boot'),('background_image','theme_bg')):
                    match=re.search(r'^'+key+r'\s*=\s*(\w+\.BMP)\s*$',config,re.M)
                    if match:
                        r,g,b=Image.open(theme/match[1]).convert('RGB').getpixel((0,0))
                        expected=((r&248)<<8)|((g&252)<<3)|(b>>3)
                        assert value(symbol)&0xffff==expected
                if boot==2:
                    q.key('esc')
                    wait(proc,lambda:state(0)==1,'persistent gallery')
                    save('09-persistent-gallery')
                    break
                before=save('02-camera')
                time.sleep(.4)
                after=save('03-motion')
                assert before!=after, 'CSI motion'
                q.key('right')
                wait(proc,lambda:state(4)==1,'dither mode')
                q.key('ret')
                wait(proc,lambda:state(16)==3,'native shutter persisted a BMP')
                wait(proc,lambda:state(36)==0,'shutter animation completed')
                q.key('ret',.8)
                wait(proc,lambda:state(4)==2,'hold OK selects grayscale')
                assert state(16)==3, 'hold must not take a second photo'
                q.key('esc')
                wait(proc,lambda:state(0)==1,'gallery')
                save('04-gallery-capture')
                q.key('left')
                wait(proc,lambda:state(12)==1,'previous photo')
                save('05-gallery-fixture')
                q.key('ret',.8)
                wait(proc,lambda:state(4)==0,'gallery selects no print')
                q.key('esc')
                wait(proc,lambda:state(0)==2,'list settings')
                save('06-settings')
                q.key('right')
                wait(proc,lambda:state(8)==1,'list selection')
                save('07-settings-selected')
                q.key('ret')
                q.key('esc')
                wait(proc,lambda:state(0)==0,'return to camera')
                if board=='z12':
                    before=save('08-front')
                    q.key('m')
                    wait(proc,lambda:state(40)==1,'native PE11 sensor switch')
                    time.sleep(.15)
                    assert save('08-selfie')!=before
            finally:
                if q:
                    try:
                        q.call('quit')
                    finally:
                        q.close()
                if proc.poll() is None:
                    proc.terminate()
                proc.wait(timeout=10)
    # Read FAT only after QEMU has released the card.
    import sys
    sys.path.insert(0,str(ROOT.parent/'tools/emu'))
    from sd_read import FAT16
    from PIL import Image
    import io
    with FAT16(card) as fat:
        payload=fat.read_file('LUM00003.BMP')
        im=Image.open(io.BytesIO(payload))
        assert im.size==(320,240) and im.format=='BMP'
        theme=hashlib.sha256(fat.read_file('THEME.INI')).hexdigest()
    report=dict(board=board,status='pass',captures=captures,photo_sha256=hashlib.sha256(payload).hexdigest(),
                theme_sha256=theme,scope='QEMU RAM app; USB/physical boot/printer/updater not implemented')
    (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS {board}: boot / motion / ink / capture / gallery / settings / cold persistence',flush=True)
    return report

def main():
    from build import tool_prefix
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--board',choices=['original','x8d','z12','all'],default='all')
    p.add_argument('--build',type=Path,default=ROOT/'build')
    p.add_argument('--qemu',type=Path,default=Path.home()/'.src/qemu-11.1.2/build/qemu-system-or1k')
    p.add_argument('--nm',default=tool_prefix()+'nm')
    p.add_argument('--theme',type=Path,default=ROOT/'theme')
    a=p.parse_args()
    a.output=a.output.resolve()
    a.output.mkdir(parents=True,exist_ok=False)
    for board in ('original','x8d','z12') if a.board=='all' else (a.board,):
        exercise(board,a.build.resolve(),a.qemu,a.output,a.nm,a.theme)

if __name__=='__main__':
    main()
