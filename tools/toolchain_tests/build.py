"""Offline builder and portable ELF/manifest validation. Never imports USB."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

from .cases import (BASE, SIZE, CODE_ADDR, CODE_LIMIT, SHIM, CASES, BY_NAME, vectors,
                    packet_layout, spr_policy)

SOURCES=Path(__file__).resolve().parent/'payloads'
FLAGS=['-mno-delay','-mhard-div','-msoft-float','-ffreestanding','-fno-builtin',
       '-fno-pic','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
       '-ffunction-sections','-fdata-sections','-std=gnu99','-Wall','-Wextra','-Werror']
OPTS=('O0','Os','O2','O3')
# Code images are padded to this boundary. The device then holds exactly the bytes
# the model is given, so a word-wide libc read that runs a few bytes past the end
# of .rodata (newlib's strcmp does) stays inside a known region. Padding is still
# verified by read-back, and it keeps almost every profile inside one USB chunk.
CODE_ALIGN=16

def pad_code(raw):
    """Pad a code image to CODE_ALIGN with the suite's deterministic fill."""
    if len(raw)%CODE_ALIGN:
        end=len(raw)+CODE_ALIGN-len(raw)%CODE_ALIGN
        raw=raw+bytes((0xa5^(i*0x3d))&0xff for i in range(len(raw),end))
    return raw
REJECTIONS={1:'unsupported __sfr access',2:'unsupported __sfr access',
            3:'unsupported __sfr access',4:'__sfr objects require numeric SPR pointers',
            5:'cannot convert between __sfr and RAM pointers'}


def sha(data): return hashlib.sha256(data).hexdigest()


def source_hashes():
    root=Path(__file__).resolve().parent
    hashes={str(p.relative_to(root)):sha(p.read_bytes()) for p in sorted(root.rglob('*'))
            if p.is_file() and p.suffix in ('.py','.c','.h','.S','.ld')}
    for name in ('test_toolchain.py','dump_firmware.py'):
        hashes['../'+name]=sha((root.parent/name).read_bytes())
    return hashes


def elf_info(data, base, limit, spr_reads=(), spr_writes=(), spi=False):
    """Validate fully linked ELF32 LE/no-delay and reconstruct allocated bytes."""
    if len(data)<52: raise ValueError('short ELF header')
    h=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
    if h[0][:7]!=b'\x7fELF\x01\x01\x01' or h[1]!=2 or h[2]!=92 or h[3]!=1 or h[7]!=1:
        raise ValueError('requires executable ELF32 little-endian OpenRISC, no-delay flags 1')
    if h[4]!=base or h[11]!=40 or not h[12] or h[13]>=h[12]:
        raise ValueError('wrong entry/section table')
    sections=[]
    for i in range(h[12]):
        offset=h[6]+i*40
        if offset+40>len(data): raise ValueError('truncated ELF sections')
        sections.append(struct.unpack_from('<10I',data,offset))
    names=sections[h[13]]
    if names[4]+names[5]>len(data): raise ValueError('truncated section strings')
    strings=data[names[4]:names[4]+names[5]]
    loaded=[]; symbols={}; occupied=[]
    for s in sections:
        name=strings[s[0]:].split(b'\0',1)[0].decode('ascii')
        if s[1]!=8 and s[4]+s[5]>len(data): raise ValueError('truncated '+name)
        if s[1] in (4,9) and s[5]: raise ValueError('unapplied relocations')
        if s[2]&2 and s[5]:
            if name not in ('.text','.rodata') or s[1]!=1 or s[2]&1:
                raise ValueError('unexpected/mutable allocated section '+name)
            if not base<=s[3]<s[3]+s[5]<=base+limit:
                raise ValueError('loaded section outside approved code region')
            if any(s[3]<end and start<s[3]+s[5] for start,end in occupied):
                raise ValueError('overlapping ELF load sections')
            occupied.append((s[3],s[3]+s[5]))
            raw=data[s[4]:s[4]+s[5]]; loaded.append((s[3],raw))
            if name=='.text':
                if s[3]&3 or len(raw)&3: raise ValueError('unaligned instructions')
                for (word,) in struct.iter_unpack('<I',raw):
                    opcode=word>>26
                    if opcode in (8,9): raise ValueError('trap/rfe instruction forbidden')
                    if opcode==0x2d:
                        # l.mfspr rD, rA, K reads SPR (rA | K); reads have no side
                        # effects. The front end materialises even a constant __sfr
                        # address into a register (always at -O0), so a register base
                        # is permitted; a statically encoded address must still be on
                        # the reviewed list for this case.
                        if not (word>>16)&31 and (word&0xffff) not in spr_reads:
                            raise ValueError('unexpected SPR read 0x%x' % (word&0xffff))
                    if opcode==0x30:
                        # l.mtspr rA, rB, K writes rB to SPR (rA | K). Unlike mfspr,
                        # bits 15:11 hold the value register, so the immediate is
                        # split across bits 25:21 (K high) and 10:0 (K low).
                        if (word>>16)&31: raise ValueError('SPR write must use a static r0 base')
                        address=((word>>21)&31)<<11 | (word&0x7ff)
                        if address not in spr_writes:
                            raise ValueError('SPR write to 0x%x is forbidden' % address)
        if s[1]==2:
            if s[6]>=len(sections) or s[9]!=16: raise ValueError('invalid symbol table')
            stringsec=sections[s[6]]; ss=data[stringsec[4]:stringsec[4]+stringsec[5]]
            for off in range(s[4],s[4]+s[5],16):
                n,value,size,info,other,index=struct.unpack_from('<IIIBBH',data,off)
                sym=ss[n:].split(b'\0',1)[0].decode('ascii')
                if n and index==0: raise ValueError('unresolved symbol '+sym)
                if sym: symbols[sym]=value
                if index==0xfff1 and value and sym not in ('SPI_read','CACHE_clean'):
                    raise ValueError('unexpected absolute symbol '+sym)
    if not loaded or not any(a==base for a,b in loaded): raise ValueError('missing entry section')
    if spi and symbols.get('SPI_read')!=0x0203D38C: raise ValueError('wrong stock SPI address')
    if not spi and 'SPI_read' in symbols: raise ValueError('unexpected vendor dependency')
    if 'CACHE_clean' in symbols and symbols['CACHE_clean']!=0x0202B058:
        raise ValueError('wrong stock cache-clean address')
    length=max(a+len(b) for a,b in loaded)-base
    raw=bytearray(length)
    for a,b in loaded: raw[a-base:a-base+len(b)]=b
    return bytes(raw),symbols


def command(argv, log):
    result=subprocess.run([str(a) for a in argv],capture_output=True,text=True)
    with log.open('a',encoding='utf8') as f:
        f.write('$ '+' '.join(map(str,argv))+'\n'+result.stdout+result.stderr+'\n')
    return result


def build_suite(output, prefix='or1k-elf-', optimizations=OPTS, seed=3292, random_count=8):
    output=Path(output)
    output.mkdir(parents=True,exist_ok=True)
    if any(output.iterdir()): raise ValueError('build output must be empty; choose a new --output directory')
    tools={name:shutil.which(prefix+name) for name in ('gcc','ld','objcopy','objdump')}
    if not all(tools.values()): raise ValueError('missing toolchain programs for prefix '+prefix)
    manifest={'version':1,'layout':dict(code=CODE_ADDR,code_limit=CODE_LIMIT,packet=BASE,size=SIZE),
              'sources':source_hashes(),'seed':seed,'random_count':random_count,'optimizations':list(optimizations),
              'compiler':subprocess.check_output([tools['gcc'],'--version'],text=True).splitlines()[0],
              'tool_paths':tools,'tests':[],
              'excluded':['Additional stock APIs: no independently vetted addresses/signatures yet.',
                          'Live SPR writes and interrupt-entry ABI: not safe in this USB callback suite.']}
    libgcc=subprocess.check_output([tools['gcc'],'-mno-delay','-print-libgcc-file-name'],text=True).strip()
    libc=subprocess.check_output([tools['gcc'],'-mno-delay','-print-file-name=libc.a'],text=True).strip()
    manifest['runtime_hashes']={str(p):sha(Path(p).read_bytes()) for p in (libgcc,libc) if Path(p).is_file()}
    failures=[]
    for ci,case in enumerate(CASES):
        for opt in optimizations:
            ident=(ci+1)*16+OPTS.index(opt)
            key=case['name']+'-'+opt; folder=output/key; folder.mkdir()
            log=folder/'build.log'
            entry=dict(name=case['name'],optimization=opt,id=ident,key=key,
                       kind=case.get('kind','ram'),description=case['description'],layout=packet_layout(case),
                       vectors=vectors(case,seed,random_count),status='error')
            manifest['tests'].append(entry)
            defines=['-DCASE='+str(case['case']),'-DOP='+str(case.get('op',0)),
                     '-DARG_COUNT='+str(case.get('argc',1)),'-DTEST_ID='+str(ident),
                     '-DINPUT_OFFSET='+str(entry['layout']['input']),
                     '-DHEADER_OFFSET='+str(entry['layout']['header']),
                     '-DOUTPUT_OFFSET='+str(entry['layout']['output']),
                     '-DPACKET_ADDR=0x%08X' % BASE]
            flags=FLAGS+['-'+opt]+([] if case.get('tail') else ['-fno-optimize-sibling-calls'])
            entry['flags']=flags+defines
            source='shim.c' if entry['kind']=='spi' else 'tests.c'
            commands=[[tools['gcc'],*flags,*defines,'-c',SOURCES/source,'-o',folder/'test.o']]
            objects=[folder/'test.o']
            if entry['kind']!='spi':
                objects.append(folder/'oracle.o')
                commands.append([tools['gcc'],'-mno-delay',*defines,'-c',SOURCES/'oracle.S','-o',objects[-1]])
            failed=False
            for cmd in commands:
                r=command(cmd,log)
                if r.returncode:
                    entry['reason']='compilation failed; see '+key+'/build.log'; failed=True; break
            if failed:
                failures.append(key); print('ERROR',key); continue
            budget=128 if entry['kind']=='spi' else entry['layout']['code_limit']
            script=folder/'test.ld'
            template=(SOURCES/('shim.ld' if entry['kind']=='spi' else 'link.ld')).read_text(encoding='utf8')
            script.write_text(template.replace('@CODE_BUDGET@',str(budget)),encoding='utf8')
            link=[tools['ld'],'--gc-sections','-T',script,
                  '-Map',folder/'test.map','-o',folder/'test.elf',*objects,libgcc]
            if case.get('libc'): link += [libc,libgcc]
            r=command(link,log)
            if r.returncode:
                if 'budget exceeded' in r.stderr or 'overflowed by' in r.stderr:
                    entry['status']='blocked_size'; entry['reason']=r.stderr.strip(); print('SIZE',key)
                else:
                    entry['reason']='link failed; see '+key+'/build.log'; failures.append(key); print('ERROR',key)
                continue
            try:
                spi=entry['kind']=='spi'; base=SHIM if spi else CODE_ADDR
                policy=spr_policy(case)
                raw,symbols=elf_info((folder/'test.elf').read_bytes(),base,budget,
                                      spr_reads=policy['reads'],spr_writes=policy['writes'],spi=spi)
                r=command([tools['objcopy'],'-O','binary',folder/'test.elf',folder/'code.bin'],log)
                if r.returncode or (folder/'code.bin').read_bytes()!=raw:
                    raise ValueError('objcopy/ELF byte reconstruction mismatch')
                dis=command([tools['objdump'],'-d',folder/'test.elf'],log)
                if dis.returncode: raise ValueError('disassembly failed')
                (folder/'test.disasm').write_text(dis.stdout,encoding='utf8')
                (folder/'payload.bin').write_bytes(pad_code(raw))
                entry.update(status='ready',code_size=len(raw),symbols=symbols,
                             files={name:dict(path=key+'/'+name,sha256=sha((folder/name).read_bytes()))
                                    for name in ('test.elf','code.bin','payload.bin','test.map','test.disasm','test.ld','build.log')})
                print('READY',key,len(raw),'bytes')
            except Exception as exc:
                entry['reason']=str(exc)
                failures.append(key); print('ERROR',key,str(exc))
    manifest['compile_rejections']=[]
    rejection_dir=output/'rejections'; rejection_dir.mkdir()
    for opt in optimizations:
        for number,diagnostic in REJECTIONS.items():
            name='reject%d-%s.log' % (number,opt); log=rejection_dir/name
            r=command([tools['gcc'],*FLAGS,'-'+opt,'-DREJECTION='+str(number),'-c',
                       SOURCES/'reject.c','-o',rejection_dir/'forbidden.o'],log)
            passed=r.returncode!=0 and diagnostic in r.stderr
            manifest['compile_rejections'].append(dict(number=number,optimization=opt,
                status='pass' if passed else 'error',path='rejections/'+name,sha256=sha(log.read_bytes())))
            if not passed: failures.append(name)
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
    if failures: raise ValueError('build errors: '+', '.join(failures))
    return manifest


def load_suite(directory, require_sources=True):
    directory=Path(directory).resolve()
    manifest=json.loads((directory/'manifest.json').read_text(encoding='utf8'))
    if manifest.get('version')!=1 or manifest.get('layout')!=dict(code=CODE_ADDR,code_limit=CODE_LIMIT,packet=BASE,size=SIZE):
        raise ValueError('unsupported manifest/layout')
    if require_sources and manifest.get('sources')!=source_hashes():
        raise ValueError('suite source hashes changed; rebuild into a new directory')
    if not isinstance(manifest.get('random_count'),int) or not 0<=manifest['random_count']<=256:
        raise ValueError('invalid random vector count')
    seen=set()
    for entry in manifest['tests']:
        if entry.get('key') in seen or entry['name'] not in BY_NAME: raise ValueError('unknown/duplicate test')
        seen.add(entry['key'])
        case=BY_NAME[entry['name']]
        expected_id=(CASES.index(case)+1)*16+OPTS.index(entry['optimization'])
        if entry['key']!=entry['name']+'-'+entry['optimization'] or entry['id']!=expected_id:
            raise ValueError('invalid test identity')
        if entry.get('layout')!=packet_layout(case): raise ValueError('invalid per-test packet layout')
        if entry['optimization'] not in OPTS or entry['kind']!=case.get('kind','ram'):
            raise ValueError('invalid test kind/optimization')
        if entry['vectors']!=vectors(case,manifest['seed'],manifest['random_count']):
            raise ValueError('input vectors do not match fixed suite generator')
        if entry['status']=='error': raise ValueError('suite contains build errors: '+entry['key'])
        if entry['status'] in ('blocked_size','blocked_safety'): continue
        if entry['status']!='ready': raise ValueError('unknown build status')
        files={}
        for name,meta in entry['files'].items():
            p=(directory/meta['path']).resolve()
            if directory not in p.parents: raise ValueError('artifact escapes suite directory')
            raw=p.read_bytes()
            if sha(raw)!=meta['sha256']: raise ValueError('artifact checksum mismatch: '+str(p))
            files[name]=raw
        spi=entry['kind']=='spi'
        policy=spr_policy(case)
        code,symbols=elf_info(files['test.elf'],SHIM if spi else CODE_ADDR,128 if spi else entry['layout']['code_limit'],
                              spr_reads=policy['reads'],spr_writes=policy['writes'],spi=spi)
        if code!=files['code.bin'] or symbols!=entry['symbols'] or len(code)!=entry['code_size']:
            raise ValueError('ELF/code/symbol metadata mismatch')
        if code!=files['payload.bin'][:len(code)] or pad_code(code)!=files['payload.bin']:
            raise ValueError('invalid payload bytes')
        entry['blob']=pad_code(code)
    opts=manifest.get('optimizations',[])
    if not opts or len(set(opts))!=len(opts) or any(o not in OPTS for o in opts):
        raise ValueError('invalid suite optimization matrix')
    if seen!={c['name']+'-'+o for c in CASES for o in opts}:
        raise ValueError('incomplete/altered suite matrix')
    rejections=manifest.get('compile_rejections',[])
    if {(r['number'],r['optimization']) for r in rejections}!={(n,o) for n in REJECTIONS for o in opts} \
            or len(rejections)!=len(REJECTIONS)*len(opts):
        raise ValueError('incomplete compiler rejection matrix')
    for r in rejections:
        p=(directory/r['path']).resolve()
        if directory not in p.parents or r['status']!='pass' or sha(p.read_bytes())!=r['sha256']:
            raise ValueError('compiler rejection evidence mismatch')
    if not seen or not any(e['status']=='ready' for e in manifest['tests']):
        raise ValueError('no runnable tests')
    return manifest
