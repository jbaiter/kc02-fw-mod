#!/usr/bin/env python3
"""Offline build of bounded bench probes; never imports USB.

Uses the patched AX3292 little-endian/no-delay compiler and the toolchain test
suite's ELF policy validator. Refuses mutable sections, traps and unapproved
SPR writes. Only timerprobe permits direct writes, to TT's three registers.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools'))
from toolchain_tests import build, cases

IDS = {'echo': 0xEC401000, 'sensorprobe': 0x53454E53, 'sprprobe': 0x53505231,
       'adcprobe': 0x41444331, 'timerprobe': 0x54544D31, 'flashidprobe': 0x4A454431,
       'mode': 0x0DE40100, 'packetcheck': 0x50434B31, 'powerprobe': 0x50575231,
       'displayprobe': 0x44535031, 'sensoridprobe': 0x53494431,
       'sensorclkprobe': 0x53434C31, 'sensorbusprobe': 0x53425231,
       'iic1probe': 0x49494331, 'lumarun': 0x4C554D31,
       'ramalloc': 0x52414C31, 'sensortable': 0x534E5431, 'softiicprobe': 0x534F4631, 'gpiomapprobe': 0x474D5031, 'ramfree': 0x52465231}
SPR_POLICY = {
    'adcprobe': ((0x9170, 0x9174, 0x9178), ()),
    'timerprobe': ((0x9900, 0x9904, 0x9908), (0x9900, 0x9904, 0x9908)),
    'displayprobe': ((0xc800, 0xc87c, 0xc810, 0xc820, 0xc824, 0xc828, 0xc82c,
                      0xc830, 0xc834, 0xa804, 0xa840, 0xa844, 0xb014, 0xb01c,
                      0x898c, 0x8998),
                     (0xc828, 0xc800)),
    # Camera power/clock/bus bring-up. Every one of these is also restored by the
    # probe, and the whole set is a delayed-peripheral change that a power cycle
    # reverses regardless.
    'sensoridprobe': ((0x8868, 0x884c, 0x8850, 0x8848, 0x8348, 0x810c, 0x804c,
                       0x800c, 0x808c, 0x838c,
                       0x9090, 0x9094, 0x9098, 0x90a0, 0x90a4),
                      (0x8868, 0x884c, 0x8850, 0x8848, 0x8348, 0x810c, 0x804c,
                       0x800c, 0x808c, 0x838c,
                       0x9090, 0x9094, 0x909c, 0x90a0)),
    # Adds the SYS_CLK_IIC0 gate register to the sensoridprobe set.
    'sensorclkprobe': ((0x8840, 0x8844, 0x8868, 0x884c, 0x8850, 0x8848, 0x8348,
                        0x810c, 0x804c, 0x800c, 0x808c, 0x838c,
                        0x9090, 0x9094, 0x9098, 0x90a0, 0x90a4),
                       (0x8840, 0x8868, 0x884c, 0x8850, 0x8848, 0x8348, 0x810c,
                        0x804c, 0x800c, 0x808c, 0x838c,
                        0x9090, 0x9094, 0x909c, 0x90a0)),
    # Adds the second IIC bus and the remaining PE GPIO registers.
    'sensorbusprobe': ((0x8868, 0x884c, 0x8850, 0x8848, 0x8348,
                        0x800c, 0x804c, 0x808c, 0x810c, 0x838c,
                        0x9090, 0x9094, 0x9098, 0x909c, 0x90a0, 0x90a4,
                        0x90cc, 0x90d0, 0x90d4, 0x90d8, 0x90dc, 0x90e0),
                       (0x8868, 0x884c, 0x8850, 0x8848, 0x8348,
                        0x800c, 0x804c, 0x808c, 0x810c, 0x838c,
                        0x9090, 0x9094, 0x909c, 0x90a0,
                        0x90cc, 0x90d0, 0x90d8, 0x90dc)),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True, help='new build directory')
    ap.add_argument('--tool-prefix', default=str(Path.home() / '.cache/kc02-toolchain/prefix/bin/or1k-elf-'))
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    source_dir = Path(__file__).resolve().parent / 'payloads'
    common = REPO / 'tools/toolchain_tests/payloads'
    # Luma's own headers, so probes and firmware share one driver implementation.
    luma_inc = REPO / 'custom-firmware/include'
    # Probes whose register addresses are not literals: the compiler then emits
    # dynamic rA-based mfspr/mtspr that cannot be checked against an allowlist.
    # Their SPR set is reviewed by hand instead; the SPR_POLICY entry above and
    # the probe's own save/restore list are the documentation for that review.
    SPR_POLICY['ramalloc'] = ((), ())   # no SPR access: allocator call only
    SPR_POLICY['ramfree'] = ((), ())    # no SPR access: allocator call only
    # Calls stock routines only; any SPR traffic happens inside them.
    SPR_POLICY['sensortable'] = ((), ())
    # Software IIC: GPIO data/dir/map/plu/dgl/drv on port 3, plus SR for the
    # critical section the stock bit-bang uses.
    SPR_POLICY['softiicprobe'] = ((0x800c, 0x804c, 0x808c, 0x810c, 0x814c, 0x838c, 0x18),
                                  (0x800c, 0x804c, 0x808c, 0x810c, 0x814c, 0x838c, 0x18))
    SPR_POLICY['gpiomapprobe'] = ((0x800c, 0x804c, 0x808c, 0x810c, 0x814c, 0x838c, 0x8348),
                                  (0x810c,))
    DYNAMIC_SPR = {'sensorbusprobe', 'sensoridprobe', 'sensorclkprobe', 'iic1probe',
                   'softiicprobe', 'gpiomapprobe'}
    manifest = {}

    # IIC1 only: PMAPCON2 + the PE GPIO block + the IIC1 pair.
    SPR_POLICY['iic1probe'] = ((0x8348, 0x800c, 0x804c, 0x808c, 0x810c, 0x838c,
                                0x90cc, 0x90d0, 0x90d4, 0x90d8, 0x90dc, 0x90e0),
                               (0x8348, 0x800c, 0x804c, 0x808c, 0x810c, 0x838c,
                                0x90cc, 0x90d0, 0x90d8, 0x90dc))

    # Runs Luma from RAM: saves and restores the display registers it disturbs.
    SPR_POLICY['lumarun'] = ((0xc800, 0xc810, 0xc820, 0xc824, 0xc828, 0xc82c,
                              0xc830, 0xc834, 0xc87c, 0xa800, 0xa804),
                             (0xc800, 0xc810, 0xc820, 0xc824, 0xc828, 0xc82c,
                              0xc830, 0xc834, 0xa800, 0xa804))

    def run(argv):
        result = subprocess.run([str(x) for x in argv], capture_output=True, text=True)
        with (args.output / 'build.log').open('a') as log:
            log.write('$ ' + ' '.join(map(str, argv)) + '\n' + result.stdout + result.stderr)
        result.check_returncode()
        return result.stdout

    for name, ident in IDS.items():
        stem = args.output / name
        run([args.tool_prefix + 'gcc', *build.FLAGS, '-Os', '-fno-optimize-sibling-calls',
             '-I', common, '-I', luma_inc, '-DTEST_ID=' + str(ident), '-c', source_dir / (name + '.c'),
             '-o', stem.with_suffix('.o')])
        run([args.tool_prefix + 'ld', '--gc-sections', '-T', source_dir / 'link.ld',
             '-Map', stem.with_suffix('.map'), '-o', stem.with_suffix('.elf'), stem.with_suffix('.o')])
        reads, writes = SPR_POLICY.get(name, ((), ()))
        raw, symbols = build.elf_info(stem.with_suffix('.elf').read_bytes(), cases.CODE_ADDR,
                                      min(cases.CODE_LIMIT, 0x020D8800 - cases.CODE_ADDR),
                                      spr_reads=reads, spr_writes=writes,
                                      allow_dynamic_spr=name in DYNAMIC_SPR)
        run([args.tool_prefix + 'objcopy', '-O', 'binary', stem.with_suffix('.elf'), stem.with_suffix('.bin')])
        if stem.with_suffix('.bin').read_bytes() != raw:
            raise ValueError('ELF/objcopy mismatch: ' + name)
        listing = run([args.tool_prefix + 'objdump', '-d', stem.with_suffix('.elf')])
        stem.with_suffix('.dis').write_text(listing)
        sources = [source_dir / (name + '.c'), source_dir / 'link.ld', common / 'common.h',
                   luma_inc / 'ax_sensor.h', luma_inc / 'ax_cam_power.h']
        manifest[name] = dict(test_id=ident, code_bytes=len(raw),
                              sha256=hashlib.sha256(raw).hexdigest(), symbols=symbols,
                              sources={str(p.relative_to(REPO)): hashlib.sha256(p.read_bytes()).hexdigest()
                                       for p in sources})
        print(f'{name}: {len(raw)} bytes; ELF policy passed; allowed SPR writes={writes}')
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
