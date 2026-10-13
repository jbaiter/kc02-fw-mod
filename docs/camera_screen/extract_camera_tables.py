#!/usr/bin/env python3
"""Extract KC02 camera tables. No device access and no firmware modification."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

SHA256 = '218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5'
BIAS = 0x01ffda00
DESCRIPTOR = 0x020c2b40
STRIDE = 48  # win_create_widget_list increments by 0x30, verified in disassembly.

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('firmware', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    fw = args.firmware.read_bytes()
    assert hashlib.sha256(fw).hexdigest() == SHA256, 'wrong firmware'
    def read(a, n):
        off = a - BIAS
        assert off >= 0 and off + n <= len(fw), hex(a)
        return fw[off:off+n]
    def u32(a): return struct.unpack('<I', read(a, 4))[0]
    def h(a): return f'0x{a:08x}'
    keymap, widgets, initial_handle, reopen = struct.unpack('<IIII', read(DESCRIPTOR, 16))
    assert (keymap, widgets, initial_handle, reopen) == (0x020c2b50, 0x020c2bd0, 0, 0)
    keys = []
    for i in range(64):
        a = keymap + i * 8
        msg, handler = struct.unpack('<II', read(a, 8))
        keys.append(dict(address=h(a), message=msg, handler=h(handler)))
        if msg == 0x2e or handler == 0:
            assert (msg, handler) == (0x2e, 0)
            break
    else: raise AssertionError('missing keymap terminator')
    rows = []
    for i in range(64):
        a = widgets + i * STRIDE
        r = read(a, STRIDE)
        u = lambda off: struct.unpack_from('<I', r, off)[0]
        if u(0) == 0: break
        assert 0x02000000 <= u(0) < 0x02080000, h(a)
        row = dict(address=h(a), create=h(u(0)), id=struct.unpack_from('<H', r, 4)[0],
                   x=struct.unpack_from('<h', r, 6)[0], y=struct.unpack_from('<h', r, 8)[0],
                   width=struct.unpack_from('<H', r, 10)[0], height=struct.unpack_from('<H', r, 12)[0],
                   style=f'0x{struct.unpack_from("<H", r, 14)[0]:04x}', bgColor=r[16],
                   image=h(u(20)), imageAlign=r[24], string=h(u(28)), strAlign=r[32],
                   fontColor=r[33], font=r[34], private=h(u(36)),
                   extra_28=h(u(40)), extra_2c=h(u(44)), raw_hex=r.hex())
        if BIAS <= u(28) < BIAS + len(fw):
            row['initial_text'] = read(u(28), min(80, len(fw) - (u(28)-BIAS))).split(b'\0')[0].decode('gbk', errors='replace')
        rows.append(row)
    else: raise AssertionError('missing widget terminator')
    assert len(rows) == 19 and len(keys) == 16
    args.output.mkdir(parents=True, exist_ok=True)
    data = dict(firmware_sha256=SHA256, cpu_to_file_bias=h(BIAS), descriptor=h(DESCRIPTOR),
                keymap_address=h(keymap), widgets_address=h(widgets), widget_stride=STRIDE,
                initial_handle=initial_handle, repeat_open_support=reopen,
                keymap=keys, widgets=rows, widget_terminator=h(a))
    (args.output/'tables.json').write_text(json.dumps(data, indent=2)+'\n')
    for name, records in [('keymap', keys), ('widgets', rows)]:
        fields = list(records[0])
        if name == 'widgets': fields += ['initial_text']
        text = '\t'.join(fields)+'\n'
        text += ''.join('\t'.join(str(r.get(k, '')) for k in fields)+'\n' for r in records)
        (args.output/(name+'.tsv')).write_text(text)
    print('Extracted 15 message bindings, root + 18 children, 48-byte records; firmware hash verified.')

if __name__ == '__main__': main()
