#!/usr/bin/env python3
"""QEMU-only first-stage relocation/checkpoint test. No USB or flash claims."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent/'tools/emu'))
from profiles import PROFILES, machine_args
from smoke import QMP, wait_for


def exercise(qemu, build, output, wait=False, card=None):
    manifest = json.loads((build/'manifest.json').read_text())
    raw = build/'bringup.ram.bin'
    if hashlib.sha256(raw.read_bytes()).hexdigest() != manifest['sha256']:
        raise ValueError('build does not match its manifest')
    if wait:
        if manifest.get('selftest') or not manifest.get('usb_vid') or not manifest.get('usb_pid'):
            raise ValueError('--usb-wait requires a USB-attached non-selftest build')
    elif not card and (not manifest.get('selftest') or manifest.get('usb_vid') or manifest.get('usb_pid')):
        raise ValueError('requires a matching USB-detached --selftest build')
    if card and manifest.get('selftest'):
        raise ValueError('the recovery gate needs the non-selftest image')
    output.mkdir(parents=True, exist_ok=True)
    sock = output/'qmp.sock'
    if sock.exists():
        raise FileExistsError('refusing an existing QMP socket')
    properties={'sensor':'off', 'entry':hex(manifest['entry']), 'stack-top':'0x027ffff0'}
    if card:
        properties['sd-image']=str(Path(card).resolve())
    command = [str(qemu), *machine_args(PROFILES['original'].options(), properties),
               '-m','64M','-nodefaults','-accel','tcg,thread=multi',
               '-display','none','-monitor','none','-serial','none','-kernel',str(raw.resolve()),
               '-qmp',f'unix:{sock},server=on,wait=off']
    qmp = None
    with (output/'qemu.log').open('wb') as log:
        proc = subprocess.Popen(command, stdout=log, stderr=log,
                                env={k:v for k,v in os.environ.items() if not k.startswith('AX3292_')})
        try:
            wait_for(proc, sock.exists, 'QMP socket')
            qmp = QMP(sock)
            def sample():
                return struct.unpack('<8I', qmp.memory(0x02002800,32,output/'observations.bin'))
            if wait or card:
                # QEMU does not model the AX3292 USB controller, so the correct
                # outcome is a stable polled wait. With a card the recovery gate
                # result and fingerprint are recordable before that wait.
                wait_for(proc, lambda: sample()[1]==3, 'USB stage reached', timeout=15)
                first = sample()
                time.sleep(.5)
                last = sample()
                assert first==last and last[0]==0x4c554d41 and last[3]==1 and last[4]==0, last
                report = dict(command=command, first=first, last=last, usb_enumerated=False,
                              reason='AX3292 USB controller is not part of the QEMU machine',
                              recovery_code=last[5], card_bytes=last[6], candidate_crc=hex(last[7]),
                              usb_tested=False, physical_hardware_tested=False, flash_written=False)
                (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
                return report
            wait_for(proc, lambda: sample()[4]>=100, '100 checkpoint resumes', timeout=15)
            first = sample()
            time.sleep(.2)
            last = sample()
            assert last[0]==0x4c554d41 and last[1]==3 and last[7]==0 and last[4]>first[4], last
            payload = raw.read_bytes()[0x3000:]
            resident = qmp.memory(0x027c0000,len(payload),output/'resident.bin')
            assert payload == resident, 'resident relocation changed code/data'
            # The selftest run has no card, so the gate must report exactly that
            # and never a fabricated success.
            assert last[5] == 1 and last[6] == 0 and last[7] == 0, last
            report = dict(command=command, first=first, last=last, relocated_bytes=len(payload),
                          recovery_code=last[5],
                          usb_tested=False, physical_hardware_tested=False, flash_written=False)
            (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
            return report
        finally:
            if qmp:
                try:
                    qmp.call('quit')
                except (OSError, RuntimeError):
                    pass
                qmp.close()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill(); proc.wait()
            sock.unlink(missing_ok=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--qemu', type=Path, default=Path.home()/'.src/qemu-11.1.2/build/qemu-system-or1k')
    p.add_argument('--build', type=Path, default=ROOT/'build/bringup-selftest')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--usb-wait', action='store_true',
                   help='USB-attached build: prove a clean polled wait, no enumeration claim')
    p.add_argument('--card', type=Path,
                   help='SD image containing DESTBIN.BIN, to exercise the recovery gate')
    a = p.parse_args()
    print(json.dumps(exercise(a.qemu,a.build,a.output.resolve(),a.usb_wait,a.card), indent=2))


if __name__ == '__main__':
    main()
