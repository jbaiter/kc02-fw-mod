#!/usr/bin/env python3
"""Run native host-mock controller tests, not stock firmware or device code."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from build_main_menu import generate_widgets
from extract_main_menu_tables import SHA256

HERE = Path(__file__).resolve().parent

def run(args):
    image = args.firmware.read_bytes()
    if hashlib.sha256(image).hexdigest() != SHA256:
        raise ValueError('wrong firmware SHA256')
    args.out.mkdir(parents=True, exist_ok=True)
    generate_widgets(image, args.out / 'main_menu_widgets.inc')
    executable = args.out / 'test_main_menu'
    command = ['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-DMENU_HOST_TEST',
               '-fsanitize=undefined','-fno-sanitize-recover=all',
               '-I' + str(args.out),'-I' + str(HERE),str(HERE / 'main_menu_screen_model.c'),
               str(HERE / 'test_main_menu.c'),'-o',str(executable)]
    subprocess.run(command, check=True)
    result = subprocess.check_output([str(executable)], text=True)
    print(result, end='')
    match = re.search(r'PASS: (\d+) host assertions', result)
    assert match
    sources = ['main_menu_screen_model.c','main_menu_screen.h','firmware_imports.def',
               'test_main_menu.c','test_main_menu.py','build_main_menu.py']
    report = dict(passed=True, assertions=int(match[1]), firmware_sha256=SHA256,
        command=command, sanitizers=['undefined-behavior sanitizer'],
        source_sha256={name:hashlib.sha256((HERE/name).read_bytes()).hexdigest() for name in sources},
        coverage=['all six valid selections, four arrows, event/flag guards',
                  'six mode routes, audio transition state, playback shortcut',
                  'enter/exit allocations, labels and renderer import arguments',
                  'six selection positions, blink threshold and background-only phase',
                  'all eight allocation failure points, including the retained stock leak'],
        limitations=['Drivers/resource decoders are mocked; no peripheral or target ABI validation.',
                     'Tests check reconstructed behavior, not whole-program binary equivalence.'])
    (args.out / 'test_results.json').write_text(json.dumps(report, indent=2) + '\n')

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--firmware',type=Path,default=HERE.parent.parent/'firmware/original.bin')
    ap.add_argument('--out',type=Path,required=True)
    run(ap.parse_args())
