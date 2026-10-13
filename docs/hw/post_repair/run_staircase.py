#!/usr/bin/env python3
"""One bounded print job using the existing guarded RAM harness; no flash writes.
Adds full-function image comparison for every direct stock payload dependency.
Do not retry a failed transport without operator power-cycle confirmation.
"""
import importlib.util
import hashlib
import json
import sys
from pathlib import Path

REPO = Path('/home/jbaiter/.src/kc02-fw-mod')
ROOT = Path('/tmp/kc02-board-re/hw/post-repair')
spec = importlib.util.spec_from_file_location('executor', '/tmp/kc02-board-re/hw/execute_payload.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
image = (REPO / 'firmware/original.bin').read_bytes()
assert hashlib.sha256(image).hexdigest() == '218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5'
functions = [
    (0x0203e5c4, 208, 'allocator complete body'),
    (0x02059f84, 416, 'printer init complete body'),
    (0x0205a6c4, 440, 'print run A complete body'),
    (0x02059290, 276, 'print power classification complete body'),
]
pins = tuple((address, image[address - 0x01ffda00:address - 0x01ffda00 + size], name)
             for address, size, name in functions)
m.dump.ROUTINE_SIGNATURES += pins
sys.argv = [sys.argv[0], '--payload', 'ramprint', '--input-words', '128,3,0,1,0,5']
rc = m.main()
report_file = Path('/tmp/kc02-board-re/hw/run_ramprint_None.json')
if report_file.exists():
    report = json.loads(report_file.read_text())
    report.update(input_words=[128,3,0,1,0,5], pattern='16 bands of 4,8,...,64 dots per feed line',
                  print_result_meaning='out[7]=0 success; 8 paper/cover, 9 low supply; visual inspection pending',
                  added_firmware_body_guards=[dict(address=hex(a), size=n, name=name) for a,n,name in functions])
    (ROOT / 'staircase.json').write_text(json.dumps(report, indent=2) + '\n')
    if rc == 0:
        reply = report['reply']; out = reply['output']
        if not reply['magic_ok'] or out[1] != 128 or out[3] != 4352:
            print('STOP: unexpected completion data; inspect before any retry.'); rc = 1
        else:
            print(f'PRINT RESULT: {out[7]}; {out[3]} ink pixels, {out[1]} feed lines.')
            print('Physical output not yet assessed. No follow-up job scheduled.')
raise SystemExit(rc)
