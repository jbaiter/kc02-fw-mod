#!/usr/bin/env python3
"""Offline build/link of the reconstructed camera screen. Does NOT access USB or flash.
The required --base is a link address, not an assertion that this RAM is free.
"""
import argparse
import csv
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path
from generate_widgets import generate, SHA256

HERE=Path(__file__).resolve().parent

def imports():
    result=[]
    for line in (HERE/'firmware_imports.def').read_text().splitlines():
        m=re.fullmatch(r'IMPORT\((.+?),\s*(\w+),\s*\((.*)\),\s*(0x[0-9a-f]+)\)',line.strip())
        if m:
            ret,name,args,address=m.groups()
            result.append(dict(name=name,return_type=ret,parameters=args,address=int(address,16)))
    if not result: raise ValueError('empty import list')
    assert len({r['name'] for r in result})==len(result)
    for r in result:
        if 0x020082f4<=r['address']<0x0200ab04:
            raise ValueError('original camera function imported: '+r['name'])
    return result

def symbols(nm,elf):
    text=subprocess.check_output([str(nm),'-n',str(elf)],text=True)
    result={}
    for line in text.splitlines():
        cols=line.split()
        if len(cols)==3: result[cols[2]]=int(cols[0],16)
    return result,text

def elf_sections(path):
    blob=path.read_bytes()
    assert blob[:7]==b'\x7fELF\x01\x01\x01','not ELF32 little-endian'
    assert struct.unpack_from('<H',blob,18)[0]==92,'not OpenRISC'
    assert struct.unpack_from('<I',blob,36)[0]&1,'missing no-delay ELF flag'
    shoff=struct.unpack_from('<I',blob,32)[0]
    entsize,num,stridx=struct.unpack_from('<HHH',blob,46)
    headers=[struct.unpack_from('<IIIIIIIIII',blob,shoff+i*entsize) for i in range(num)]
    st=headers[stridx];names=blob[st[4]:st[4]+st[5]]
    out={}
    for h in headers:
        name=names[h[0]:].split(b'\0')[0].decode()
        out[name]=dict(type=h[1],flags=h[2],address=h[3],offset=h[4],size=h[5],data=blob[h[4]:h[4]+h[5]] if h[1]!=8 else b'')
    return out

def build(args):
    if args.base<0 or args.base>0xffffffff or args.base%32:
        raise ValueError('--base must be a 32-byte aligned 32-bit address')
    args.out.mkdir(parents=True,exist_ok=True)
    original_widgets=generate(args.firmware,args.out/'camera_widgets.inc')
    im=imports()
    sources=['camera_screen_model.c','camera_screen.h','firmware_imports.def','generate_widgets.py','build_camera_screen.py','entrypoints.tsv']
    elf=args.out/'camera_screen.elf';obj=args.out/'camera_screen.o';binary=args.out/'camera_screen.bin'
    ld=['OUTPUT_FORMAT("elf32-or1kle")','OUTPUT_ARCH(or1k)','ENTRY(camera_mode_enter)',
        *(f"{r['name']} = 0x{r['address']:08x};" for r in im),
        'SECTIONS {',f' . = 0x{args.base:08x};',
        ' .text : { *(.text .text.*) }', ' . = ALIGN(32);',
        ' .rodata : { *(.rodata .rodata.*) }', ' . = ALIGN(32);',
        ' .data : { *(.data .data.*) }', ' . = ALIGN(32);',
        ' .bss : { *(.bss .bss.*) *(COMMON) }',
        ' /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }','}']
    script=args.out/'camera_screen.ld';script.write_text('\n'.join(ld)+'\n')
    def tool(name): return args.tool_prefix+name
    commands=[
        [tool('gcc'),'-mno-delay','-mhard-div','-Os','-std=gnu11','-ffreestanding','-fno-builtin','-fno-common','-fno-pic','-fno-stack-protector','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror','-I'+str(args.out),'-I'+str(HERE),'-c',str(HERE/'camera_screen_model.c'),'-o',str(obj)],
        [tool('ld'),'-T',str(script),'-Map='+str(args.out/'camera_screen.map'),str(obj),'-o',str(elf)],
        [tool('objcopy'),'-O','binary',str(elf),str(binary)]]
    for command in commands: subprocess.run(command,check=True)
    sym,nmtext=symbols(tool('nm'),elf)
    (args.out/'camera_screen.nm').write_text(nmtext)
    undefined=subprocess.check_output([tool('nm'),'-u',str(elf)],text=True).strip()
    assert not undefined,'undefined symbols: '+undefined
    sections=elf_sections(elf)
    assert sections.get('.bss',{}).get('size',0)==0,'BSS initialization needed'
    widgets_section=next(s for s in sections.values() if s['address']<=sym['camera_widgets']<s['address']+s['size'])
    offset=sym['camera_widgets']-widgets_section['address']
    assert widgets_section['data'][offset:offset+960]==original_widgets,'widget bytes differ'
    key_section=next(s for s in sections.values() if s['address']<=sym['camera_bindings']<s['address']+s['size'])
    offset=sym['camera_bindings']-key_section['address'];bindings=key_section['data'][offset:offset+128]
    expected=[(0xe,'camera_window_enter'),(0xf,'camera_window_exit'),(0x10,'camera_window_refresh'),(0x1b,'camera_up'),(0x1c,'camera_down'),(0x1e,'camera_right'),(0x1d,'camera_left'),(0x1a,'camera_ok'),(0x23,'camera_ok'),(0x24,'camera_back'),(1,'camera_msg_storage'),(2,'camera_msg_battery'),(3,'camera_msg_battery_conditional'),(6,'camera_msg_noop'),(0xa,'camera_msg_clock'),(0x2e,None)]
    for i,(msg,name) in enumerate(expected):
        assert struct.unpack_from('<II',bindings,i*8)==(msg,sym[name] if name else 0),'keymap relocation mismatch'
    rows=list(csv.DictReader((HERE/'entrypoints.tsv').open(),delimiter='\t'))
    assert len(rows)==33
    for r in rows: assert r['symbol'] in sym,r['symbol']
    dis=subprocess.check_output([tool('objdump'),'-d',str(elf)],text=True)
    (args.out/'camera_screen.dis').write_text(dis)
    direct=[]
    for target in re.findall(r'\bl\.jal\s+(?:0x)?([0-9a-f]+)',dis):
        value=int(target,16)
        assert not 0x020082f4<=value<0x0200ab04,'direct call into original camera region'
        direct.append(value)
    # The three existing mode callbacks are the minimum entry hooks. No patch is applied.
    replacements={r['symbol']:dict(original=r['original_address'],replacement=f"0x{sym[r['symbol']]:08x}") for r in rows}
    hooks=[dict(slot=f'0x{slot:08x}',**replacements[name]) for slot,name in [(0x020c2adc,'camera_mode_enter'),(0x020c2ae0,'camera_mode_exit'),(0x020c2ae4,'camera_mode_tick')]]
    image=args.firmware.read_bytes()
    for hook in hooks:
        assert struct.unpack_from('<I',image,int(hook['slot'],16)-0x01ffda00)[0]==int(hook['original'],16),'stock mode callback bytes mismatch'
    manifest=dict(firmware_sha256=SHA256,elf32_little_endian_openrisc=True,base=f'0x{args.base:08x}',binary_size=binary.stat().st_size,
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),entrypoints=replacements,mode_callback_hook_candidates=hooks,
        imports=[{**r,'address':f"0x{r['address']:08x}"} for r in im],
        original_external_camera_callsites=[dict(callsite='0x0200e1c8',target='0x02008550',note='An existing non-camera call to the shared file-opening helper remains stock; do not erase the old region.')],
        widget_bytes_identical=True,keymap_points_to_recompiled_handlers=True,undefined_symbols=[],bss_size=0,
        source_sha256={n:hashlib.sha256((HERE/n).read_bytes()).hexdigest() for n in sources},build_commands=commands,
        current_ram_harness_code_budget=5632,fits_current_ram_harness=binary.stat().st_size<=5632,
        deployment_allowed=False,placement_verified=False,on_device_tested=False,
        limitations=['Link base does not reserve RAM; stock allocator may own it.','Mode hook candidates require runtime verification and cache handling.','Stock imports remain firmware-specific; host tests do not validate target ABI.','Capture error paths are hardened and not byte-identical to the original.'])
    (args.out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Built {elf}: {binary.stat().st_size} bytes; all 33 entry points; no undefined symbols; widget and handler-table checks PASS.')
    print('OFFLINE ONLY: no RAM placement, hooks, hardware execution or flash validated.')
    return manifest

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--firmware',type=Path,default=HERE.parent.parent/'firmware/original.bin')
    ap.add_argument('--tool-prefix',default='/tmp/ax3292-toolchain/prefix/bin/or1k-elf-')
    ap.add_argument('--base',type=lambda x:int(x,0),required=True)
    ap.add_argument('--out',type=Path,required=True)
    build(ap.parse_args())

if __name__=='__main__': main()
