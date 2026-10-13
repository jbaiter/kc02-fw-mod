#!/usr/bin/env python3
"""Generate typed host wrappers for the stock-import mock; no hardware access."""
from pathlib import Path
from build_camera_screen import imports

def generate(out: Path):
    im=imports()
    enum=['enum MockImport {']+[f' MOCK_{r["name"]},' for r in im]+[' MOCK_IMPORT_COUNT','};']
    (out/'mock_ids.h').write_text('\n'.join(enum)+'\n')
    code=[]
    for r in im:
        params=[] if r['parameters']=='void' else r['parameters'].split(', ')
        vararg=bool(params and params[-1]=='...')
        named=params[:-1] if vararg else params
        names=[p.split()[-1].lstrip('*') for p in named]
        code.append(f'{r["return_type"]} {r["name"]}({r["parameters"]}) {{')
        if names:
            code.append(' camera_word mock_args[16]={'+','.join(f'(camera_word){n}' for n in names)+'};')
        else: code.append(' camera_word mock_args[16]={0};')
        code.append(f' size_t mock_n={len(names)};')
        if vararg:
            code.append(' va_list ap; va_start(ap,argc); for(uint32_t i=0;i<argc;++i) mock_args[mock_n++]=va_arg(ap,unsigned int); va_end(ap);')
        call=f'mock_call(MOCK_{r["name"]},mock_args,mock_n)'
        code.append(f' (void){call};' if r['return_type']=='void' else f' return ({r["return_type"]}){call};')
        code.append('}')
    (out/'mock_wrappers.inc').write_text('\n'.join(code)+'\n')

if __name__=='__main__':
    import argparse
    ap=argparse.ArgumentParser();ap.add_argument('out',type=Path);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True);generate(a.out)
