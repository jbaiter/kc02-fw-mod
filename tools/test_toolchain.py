#!/usr/bin/env python3
"""Build/check offline or run bounded AX3292 compiler tests on a KC02 via USB."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import secrets
import sys

import dump_firmware as dump
from toolchain_tests.build import build_suite, load_suite, sha, OPTS
from toolchain_tests.cases import BASE, SIZE, CODE_ADDR, DISPATCH, make_packet, validate_result
from toolchain_tests.transport import TestTransport
from toolchain_tests.cache import ICACHE_SITE

ROOT=Path(__file__).resolve().parents[1]
DEFAULT_SUITE=ROOT/'patched/toolchain-tests/build'


def save_report(path, report):
    temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    os.replace(temporary,path)


def check_reference(reference):
    dump.validate_image(reference)
    for address,signature,name in (*dump.ROUTINE_SIGNATURES,ICACHE_SITE):
        offset=address-0x02000000+0x2600
        if reference[offset:offset+len(signature)]!=signature:
            raise ValueError('reference dump does not match the researched '+name)


def run_suite(suite, dev, reference, output, repeat=1, only=None):
    """Caller supplies a claimed transport; stop at first failure, with partial evidence."""
    check_reference(reference)
    selected=[e for e in suite['tests'] if only is None or e['key'] in only or e['name'] in only]
    if not selected or (only is not None and set(only)-{value for e in selected for value in (e['key'],e['name'])}):
        raise ValueError('unknown/empty --only selection')
    output=Path(output)
    output.mkdir(parents=True,exist_ok=False)
    suite_evidence={k:v for k,v in suite.items() if k!='tests'}
    suite_evidence['tests']=[{k:v for k,v in e.items() if k!='blob'} for e in suite['tests']]
    (output/'suite.json').write_text(json.dumps(suite_evidence,indent=2)+'\n',encoding='utf8')
    report={'version':1,'started':datetime.now(timezone.utc).isoformat(),'status':'running',
            'suite_file':'suite.json',
            'compiler':suite['compiler'],'source_hashes':suite['sources'],
            'runtime_hashes':suite['runtime_hashes'],'layout':suite['layout'],'reference_sha256':sha(reference),
            'seed':suite['seed'],'random_count':suite['random_count'],'repeat':repeat,
            'camera':dump.device_label(dev.dev),
            'firmware_signatures':[dict(address=a,bytes=b.hex(),name=n) for a,b,n in (*dump.ROUTINE_SIGNATURES,ICACHE_SITE)],
            'scope':'all' if only is None else 'selected',
            'selection':None if only is None else list(only),
            'not_selected':[e['key'] for e in suite['tests'] if e not in selected],
            'not_run':[dict(key=e['key'],reason=e['reason']) for e in selected if e['status']!='ready'],
            'excluded':suite['excluded'],'results':[],'stage':'firmware checks'}
    path=output/'run.json'; save_report(path,report)
    current=None; nonce=secrets.randbits(32); sequence=0; vr=None
    try:
        dev.check_firmware()
        report['stage']='assembly canary'; save_report(path,report)
        baseline=dev.baseline(); (output/'canary.bin').write_bytes(baseline)
        # Independent already-researched adapter baseline, before compiled payloads.
        baseline_entry=dict(key='assembly-spi',name='assembly-spi',kind='spi',blob=dump.SHIM_BYTES)
        entries=[baseline_entry]+[e for e in selected if e['status']=='ready']
        for iteration in range(repeat):
            for entry in entries:
                report['stage']='canary before '+entry['key']; save_report(path,report)
                dev.baseline()
                if entry['kind']=='spi':
                    work=[(off,None,None) for off in (0,2048)]
                else:
                    work=[]
                    for index,a in enumerate(entry['vectors']):
                        sequence+=1
                        packet=make_packet(a,(nonce+sequence)&0xffffffff,index,entry.get('layout'))
                        work.append((0,index,packet))
                for parameter,index,packet in work:
                    n=len(report['results'])
                    filename='%04d-%s-r%d-v%s.bin' % (n,entry['key'],iteration,parameter if index is None else index)
                    current={'key':entry['key'],'iteration':iteration,'vector':index,'parameter':parameter,
                             'status':'running','raw_file':filename,'stage':'upload',
                             'program_sha256':sha(entry['blob'])}
                    report['results'].append(current)
                    report['stage']=entry['key']; save_report(path,report)
                    dev.set_profile(entry,packet)
                    if entry['kind']=='spi':
                        shim=entry['blob']; buf=dump.CANARY_PATTERN
                        current['upload_sha256']=sha(buf)
                        dev.upload(dump.SHIM_ADDR,shim)
                        dev.upload(BASE,buf)
                    else:
                        # Code and packet are separate regions, so every USB
                        # transfer stays at the proven SIZE-byte size.
                        shim=DISPATCH
                        current['upload_sha256']=sha(packet)
                        dev.upload(dump.SHIM_ADDR,shim)
                        dev.upload(CODE_ADDR,entry['blob'])
                        dev.upload(BASE,packet)
                    current['stage']='invalidating instruction cache'; save_report(path,report)
                    dev.invalidate_code()
                    current['stage']='executing callback'; save_report(path,report)
                    actual=dev._command(BASE,SIZE,dump.SHIM_ADDR,parameter)
                    (output/filename).write_bytes(actual)
                    current['stage']='validating results'; save_report(path,report)
                    if entry['kind']=='spi':
                        if actual!=reference[parameter:parameter+SIZE]:
                            raise ValueError('SPI chunk differs from reference at 0x%x' % parameter)
                    else:
                        values=validate_result(entry,packet,actual)
                        current['values']=values
                        if entry['name']=='sfr_read':
                            if vr is not None and vr!=values[0]: raise ValueError('SPR_VR changed between tests')
                            vr=values[0]; report['spr_vr']=vr
                    if dev.read_memory(dump.SHIM_ADDR,len(shim))!=shim or \
                            dev.read_memory(dump.CANARY_ADDR,4)!=dump.CANARY_BYTES:
                        raise ValueError('shim/canary code guard changed')
                    if entry['kind']=='ram' and \
                            dev.read_region(CODE_ADDR,len(entry['blob']))!=entry['blob']:
                        raise ValueError('code region changed during execution')
                    current.update(status='pass',stage='complete',received_sha256=sha(actual))
                    save_report(path,report)
                print('PASS',entry['key'],'(%d vectors)' % len(work),flush=True)
        report['stage']='final assembly canary'; save_report(path,report)
        dev.baseline()
        report['status']='incomplete' if report['not_run'] else 'pass'
        report['stage']='complete'
    except BaseException as exc:
        report['status']='interrupted' if isinstance(exc,KeyboardInterrupt) else 'fail'
        report['error']=str(exc) or type(exc).__name__
        if current and current['status']=='running':
            current.update(status='fail',error=report['error'])
        if dev.last_rx:
            try: (output/'last-transfer.bin').write_bytes(dev.last_rx)
            except OSError as save_error:
                print('WARNING: could not save last transfer:',save_error,file=sys.stderr)
        raise
    finally:
        report['last_command']=dev.last_command
        report['finished']=datetime.now(timezone.utc).isoformat()
        report['passed_vectors']=sum(r['status']=='pass' for r in report['results'])
        try: save_report(path,report)
        except OSError as save_error:
            if report['status'] in ('fail','interrupted'):
                print('WARNING: could not save failure report:',save_error,file=sys.stderr)
            else: raise
    return report


def parser():
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='command',required=True)
    b=sub.add_parser('build',help='offline: compile every test and optimization variant')
    b.add_argument('--tool-prefix',default=os.environ.get('OR1K_TOOL_PREFIX','or1k-elf-'))
    b.add_argument('--output',type=Path,default=DEFAULT_SUITE)
    b.add_argument('--optimizations',default=','.join(OPTS),help='comma-separated O0,Os,O2,O3')
    b.add_argument('--seed',type=int,default=3292)
    b.add_argument('--random-vectors',type=int,default=8)
    for command in ('check','list-tests','run'):
        s=sub.add_parser(command,help={'check':'offline: validate all suite artifacts',
            'list-tests':'offline: show runnable and size-blocked tests',
            'run':'execute reviewed RAM test suite on a connected KC02'}[command])
        s.add_argument('--suite',type=Path,default=DEFAULT_SUITE)
        if command=='check': s.add_argument('--model',action='store_true',help='also run bounded instruction-model regressions (not hardware)')
        if command=='run':
            s.add_argument('--reference',type=Path,default=ROOT/'firmware/original.bin')
            s.add_argument('--output',type=Path,default=None,help='new evidence directory; never overwrite one')
            s.add_argument('--device',type=dump.positive_int)
            s.add_argument('--timeout',type=dump.positive_int,default=5000)
            s.add_argument('--repeat',type=dump.positive_int,default=1)
            s.add_argument('--only',help='comma-separated profile keys or test names, e.g. echo-O0 or echo; records selected scope')
            s.add_argument('--ack-ram-execution',action='store_true',help='acknowledge RAM execution can hang the camera and require power cycling')
            s.add_argument('--allow-blocked',action='store_true',help='return success for runnable subset despite explicitly reported size/safety exclusions; never executes blocked tests')
            s.add_argument('-v','--verbose',action='store_true')
            s.add_argument('--trace',action='store_true')
    sub.add_parser('devices',help='list USB cameras without claiming interfaces')
    return p


def main(argv=None):
    for stream in (sys.stdout,sys.stderr):
        if hasattr(stream,'reconfigure'): stream.reconfigure(encoding='utf-8')
    args=parser().parse_args(argv)
    try:
        if args.command=='build':
            opts=tuple(args.optimizations.split(','))
            if not opts or len(set(opts))!=len(opts) or any(o not in OPTS for o in opts):
                raise ValueError('invalid optimization list')
            if not 0<=args.random_vectors<=256: raise ValueError('--random-vectors must be 0..256')
            manifest=build_suite(args.output,args.tool_prefix,opts,args.seed,args.random_vectors)
            print('Built suite:',args.output)
            print('Ready:',sum(e['status']=='ready' for e in manifest['tests']),
                  'Size-blocked:',sum(e['status']=='blocked_size' for e in manifest['tests']))
            return 0
        if args.command=='devices':
            usb,backend=dump.load_usb(); cameras,webcams=dump.find_cameras(usb,backend)
            for i,dev in enumerate(cameras,1): print('%d: %s' % (i,dump.device_label(dev)))
            for dev in webcams: print('webcam-only:',dump.device_label(dev))
            if not cameras and not webcams: print('No KC02 devices found.')
            return 0
        suite=load_suite(args.suite)
        if args.command=='list-tests':
            for e in suite['tests']:
                print('%-24s %-12s %s' % (e['key'],e['status'],str(e.get('code_size','-'))+' bytes'))
            return 0
        if args.command=='check':
            print('Artifact checks passed:',len(suite['tests']),'profiles')
            if args.model:
                from toolchain_tests.model import check_model
                print('Instruction-model vectors passed:',check_model(suite))
            return 0
        if not args.ack_ram_execution:
            raise ValueError('run requires --ack-ram-execution; read docs/TOOLCHAIN_TESTS.md first')
        if args.repeat>100: raise ValueError('--repeat must be 1..100')
        reference=args.reference.read_bytes(); check_reference(reference)
        output=args.output or ROOT/'patched/toolchain-tests/runs'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
        if output.exists(): raise ValueError('evidence directory already exists; use a new --output')
        only=tuple(args.only.split(',')) if args.only else None
        if only and set(only)-{value for e in suite['tests'] for value in (e['key'],e['name'])}:
            raise ValueError('unknown --only selection')
        for e in suite['tests']:
            if (only is None or e['key'] in only or e['name'] in only) and e['status']!='ready':
                print('NOT RUN',e['key'],'('+e['status']+')')
        usb,backend=dump.load_usb(); cameras,webcams=dump.find_cameras(usb,backend)
        camera=dump.select_camera(cameras,webcams,args.device)
        print('Camera:',dump.device_label(camera))
        print('RAM execution only, except vetted flash reads. Stop/power-cycle on failure.')
        with TestTransport(usb,camera,args.timeout,args.verbose,args.trace) as dev:
            report=run_suite(suite,dev,reference,output,args.repeat,only)
        print('%s: %d passed vectors; %d blocked profiles not run. Evidence: %s' %
              (report['status'].upper(),report['passed_vectors'],len(report['not_run']),output))
        return 2 if report['not_run'] and not args.allow_blocked else 0
    except KeyboardInterrupt:
        print('Interrupted. Stop and power-cycle before retrying; partial evidence retained.',file=sys.stderr)
        return 130
    except Exception as exc:
        print('ERROR:',str(exc),file=sys.stderr)
        if args.command=='run':
            print('No automatic retry/reset. If USB execution started, power-cycle before retrying.',file=sys.stderr)
        return 1


if __name__=='__main__':
    sys.exit(main())
