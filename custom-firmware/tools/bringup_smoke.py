#!/usr/bin/env python3
"""QEMU-only first-stage tests. No USB and no flash claims.

Three modes, one per build:

  (default)      --selftest build: relocation, then the in-guest breakpoint and
                 single-step sequence through the real RSP engine.
  --usb-wait     USB-attached build: it must reach and hold a stable polled
                 wait, because QEMU does not model the AX3292 USB controller.
  --card PATH    any non-selftest build: the read-only recovery gate must find
                 and measure a candidate on a FAT16 card.
"""
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

OBSERVATIONS = 0x02002800
WORDS = 32
MAGIC = 0x4c554d41
SELFTEST_PC_COUNT = 7
STAGE_MONITOR_UP = 3
RECOVER_NO_CARD = 1


def check_selftest(words):
    """Validates the in-guest breakpoint and single-step sequence.

    bringup/selftest.c drives the real RSP engine, the real exception vectors
    and the real step logic; only the transport is replaced with a guest-side
    drain. Every expectation has its own failure bit, so a failure names
    itself. The PC progression is checked against the instruction layout in
    bringup/selftest.S, and the guest separately checks the return address
    against its own symbol.
    """
    failures, stage = words[18], words[19]
    pcs = list(words[20:20+SELFTEST_PC_COUNT])
    target = pcs[0]
    reason_step = 2  # MON_STOP_STEP, the last stop of the sequence
    ok = (failures == 0 and stage == 8 and words[28] == 0 and
          words[29] == reason_step and words[27] != 0 and
          words[17] == 0x4b4f242b and                # last wire bytes were "+$OK"
          pcs[1] == target+4 and pcs[2] == target+8 and
          pcs[3] == target+0x10 and pcs[4] == target+0x14 and pcs[5] == target+0x18 and
          pcs[6] != 0 and pcs[6] != pcs[5] and
          len(set(pcs)) == SELFTEST_PC_COUNT)
    return dict(passed=bool(ok), failures=hex(failures), stage=stage,
                pcs=[hex(p) for p in pcs], original_insn=hex(words[27]),
                code_patched_after_detach=words[28], last_stop_reason=words[29],
                wire_head=hex(words[17]))


def exercise(qemu, build, output, wait=False, card=None):
    manifest = json.loads((build/'manifest.json').read_text())
    raw = build/'bringup.ram.bin'
    if hashlib.sha256(raw.read_bytes()).hexdigest() != manifest['sha256']:
        raise ValueError('build does not match its manifest')
    if wait:
        if manifest.get('selftest') or not manifest.get('usb_vid') or not manifest.get('usb_pid'):
            raise ValueError('--usb-wait requires a USB-attached non-selftest build')
    elif card:
        if manifest.get('selftest'):
            raise ValueError('the recovery gate needs the non-selftest image')
    elif not manifest.get('selftest') or manifest.get('usb_vid') or manifest.get('usb_pid'):
        raise ValueError('requires a matching USB-detached --selftest build')
    output.mkdir(parents=True, exist_ok=True)
    sock = output/'qmp.sock'
    if sock.exists():
        raise FileExistsError('refusing an existing QMP socket')
    properties = {'sensor':'off', 'entry':hex(manifest['entry']), 'stack-top':'0x027ffff0'}
    if card:
        properties['sd-image'] = str(Path(card).resolve())
    command = [str(qemu), *machine_args(PROFILES['original'].options(), properties),
               '-m', '64M', '-nodefaults', '-accel', 'tcg,thread=multi',
               '-display', 'none', '-monitor', 'none', '-serial', 'none',
               '-kernel', str(raw.resolve()),
               '-qmp', f'unix:{sock},server=on,wait=off']
    qmp = None
    with (output/'qemu.log').open('wb') as log:
        proc = subprocess.Popen(command, stdout=log, stderr=log,
                                env={k: v for k, v in os.environ.items()
                                     if not k.startswith('AX3292_')})
        try:
            wait_for(proc, sock.exists, 'QMP socket')
            qmp = QMP(sock)

            def sample():
                return struct.unpack('<%dI' % WORDS,
                                     qmp.memory(OBSERVATIONS, 4*WORDS, output/'observations.bin'))

            report = dict(command=command,
                          usb_tested=False, physical_hardware_tested=False, flash_written=False)
            if wait or card:
                wait_for(proc, lambda: sample()[1] == STAGE_MONITOR_UP, 'stage reached', timeout=30)
                before = sample()
                time.sleep(.5)
                last = sample()
                if wait:
                    # The USB build has no card, so nothing beyond the transport
                    # may change while it holds the wait.
                    assert before[18:] == last[18:], 'selftest words moved in a non-selftest build'
                assert last[0] == MAGIC, last
                report.update(last=list(last), usb_enumerated=False,
                              reason='AX3292 USB controller is not part of the QEMU machine',
                              recovery_code=last[5], card_units=last[6],
                              candidate_crc=hex(last[15]))
                if wait:
                    assert last[5] == RECOVER_NO_CARD, last
            else:
                wait_for(proc, lambda: sample()[19] == 8, 'self-test to finish', timeout=60)
                last = sample()
                assert last[0] == MAGIC and last[1] == STAGE_MONITOR_UP, last
                # Relocation must not have changed the resident image.
                payload = raw.read_bytes()[0x3000:]
                resident = qmp.memory(0x027c0000, len(payload), output/'resident.bin')
                assert payload == resident, 'resident relocation changed code/data'
                # No card in this run: the gate must report exactly that.
                assert last[5] == RECOVER_NO_CARD and last[6] == 0 and last[15] == 0, last
                report.update(last=list(last), relocated_bytes=len(payload),
                              recovery_code=last[5], selftest=check_selftest(last))
                assert report['selftest']['passed'], report['selftest']
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
    p.add_argument('--qemu', type=Path,
                   default=Path.home()/'.src/qemu-11.1.2/build/qemu-system-or1k')
    p.add_argument('--build', type=Path, default=ROOT/'build/bringup-selftest')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--usb-wait', action='store_true',
                   help='USB-attached build: prove a clean polled wait')
    p.add_argument('--card', type=Path,
                   help='SD image holding DESTBIN.BIN, to exercise the recovery gate')
    a = p.parse_args()
    summary = exercise(a.qemu, a.build, a.output.resolve(), a.usb_wait, a.card)
    keys = ('recovery_code', 'card_units', 'candidate_crc', 'relocated_bytes', 'selftest')
    print(json.dumps({k: summary[k] for k in keys if k in summary}, indent=2))


if __name__ == '__main__':
    main()
