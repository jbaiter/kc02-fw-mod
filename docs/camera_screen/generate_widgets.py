#!/usr/bin/env python3
"""Generate the writable 48-byte KC02 widget table from hash-pinned image bytes."""
import hashlib
import struct
from pathlib import Path

SHA256='218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5'
START=0x020c2bd0
BIAS=0x01ffda00
COUNT=20

def generate(firmware: Path, output: Path):
    image=firmware.read_bytes()
    if hashlib.sha256(image).hexdigest()!=SHA256:
        raise ValueError('firmware SHA256 mismatch')
    blob=image[START-BIAS:START-BIAS+COUNT*48]
    assert len(blob)==COUNT*48
    out=['/* Generated from original.bin, including the terminating record. */',
         'CameraWidget camera_widgets[20]={']
    for i in range(COUNT):
        r=blob[i*48:(i+1)*48]
        u=lambda off:struct.unpack_from('<I',r,off)[0]
        s=lambda off:struct.unpack_from('<h',r,off)[0]
        h=lambda off:struct.unpack_from('<H',r,off)[0]
        assert (0x02000000<=u(0)<0x02080000) if i<19 else u(0)==0
        arr=lambda b:'{'+','.join('0x%02x'%v for v in b)+'}'
        values=[f'0x{u(0):08x}U',str(h(4)),str(s(6)),str(s(8)),str(h(10)),str(h(12)),str(h(14)),
                str(r[16]),arr(r[17:20]),f'0x{u(20):08x}U',str(r[24]),arr(r[25:28]),
                f'0x{u(28):08x}U',str(r[32]),str(r[33]),str(r[34]),str(r[35]),
                f'0x{u(36):08x}U',arr(r[40:48])]
        out.append('    {'+','.join(values)+'}, /* 0x%08x ID 0x%04x */'%(START+i*48,h(4)))
    out.append('};')
    output.write_text('\n'.join(out)+'\n')
    return blob

if __name__=='__main__':
    import argparse
    ap=argparse.ArgumentParser();ap.add_argument('firmware',type=Path);ap.add_argument('output',type=Path)
    a=ap.parse_args();generate(a.firmware,a.output)
