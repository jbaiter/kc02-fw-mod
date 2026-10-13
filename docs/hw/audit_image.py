#!/usr/bin/env python3
"""Zero-risk audit: read the running image out of SDRAM and compare it with the flash dump,
then structurally validate every model element we have typed in Ghidra.

Reads only (stock 0xCD command, CALLBACK_SKIP, no RAM writes, no code execution).
The declared read window is the loaded application image (file 0x2600..0x0D3200 maps to
CPU 0x02000000..0x020D0C00) plus the runtime-state regions we already observe.
"""
import hashlib, json, os, struct, sys, datetime
from pathlib import Path

REPO = Path('/home/jbaiter/.src/kc02-fw-mod')
sys.path.insert(0, str(REPO / 'tools'))
import dump_firmware as dump
from toolchain_tests import cache

IMG = (REPO / 'firmware' / 'original.bin').read_bytes()
BASE = 0x01FFDA00
OUT = Path('/tmp/kc02-board-re/hw/audit01')
CHUNK = 2048
IMAGE = (0x02000000, 0x020D0C00)          # loaded application image in SDRAM
STATE = [                                  # runtime state we already model
    ("handler_table", 0x020CCC8C, 0x0B8), ("task_array", 0x020CCD5C, 0xF0),
    ("device_list", 0x020C259C, 12 * 0x18), ("key_adc_table", 0x020C268C, 6 * 8),
    ("app_context", 0x020D416C, 0x78), ("sensor_item", 0x020CB3F0, 0x50),
    ("printer_counter", 0x020D2EC8, 4), ("ui_queue", 0x020D41E0, 0x30),
    ("keypad", 0x020C87A4, 0x20), ("game_state", 0x020CD69C, 0x40),
]


class AuditTransport(dump.CameraTransport):
    def _command(self, buffer, length, callback=dump.CALLBACK_SKIP, offset=0, payload=None):
        if not self.claimed:
            raise dump.DumpError('USB interface has not been claimed.')
        if payload is not None or callback != dump.CALLBACK_SKIP or offset:
            raise dump.DumpError('audit is read-only: no writes, no code execution.')
        pinned = {(a, len(b)) for a, b, _ in (*dump.ROUTINE_SIGNATURES, cache.ICACHE_SITE)}
        in_image = IMAGE[0] <= buffer and buffer + length <= IMAGE[1]
        in_state = any(buffer >= a and buffer + length <= a + l for _, a, l in STATE)
        if (buffer, length) not in pinned and not in_image and not in_state:
            raise dump.DumpError(f'read 0x{buffer:08X}+{length} outside the declared audit window')
        self.tag = (self.tag + 1) & 0xFFFFFFFF
        cdb = bytes([0xCD]) + struct.pack('<III', dump.MEMORY_TRANSFER_FN, buffer, callback)
        cbw = struct.pack('<4sIIBBB', b'USBC', self.tag, length, 0x80, 0, 16) + cdb + b'\0\0\0'
        self._write(cbw)
        data = self._read_exact(length)
        sig, tag, residue, status = struct.unpack('<4sIIB', self._read_exact(13))
        if (sig, tag, residue, status) != (b'USBS', self.tag, 0, 0):
            raise dump.DumpError(f'bad USB status {(sig, tag, residue, status)}; power-cycle before retrying')
        return data


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    usb, backend = dump.load_usb()
    cams, webcams = dump.find_cameras(usb, backend)
    dev = dump.select_camera(cams, webcams, 1)
    report = dict(device=dump.device_label(dev), started=datetime.datetime.now(datetime.timezone.utc).isoformat())
    ram = bytearray()
    with AuditTransport(usb, dev, 5000, verbose=bool(os.environ.get('DUMP_VERBOSE'))) as t:
        dump.check_firmware(t)
        lo, hi = IMAGE
        for a in range(lo, hi, CHUNK):
            n = min(CHUNK, hi - a)
            ram += t._command(a, n)
            if (a - lo) % (CHUNK * 64) == 0:
                print(f"  image read 0x{a:08X} ({100*(a-lo)//(hi-lo)}%)", flush=True)
        (OUT / 'image_ram.bin').write_bytes(bytes(ram))
        print(f"  image: read {len(ram)} bytes from 0x{lo:08X}, sha256={hashlib.sha256(ram).hexdigest()[:16]}")
        state = {}
        for name, a, l in STATE:
            data = b''
            for off in range(0, l, CHUNK):
                data += t._command(a + off, min(CHUNK, l - off))
            state[name] = data
            (OUT / f'{name}.bin').write_bytes(data)
            print(f"  state {name:<14} 0x{a:08X}+{l:<4} {data[:16].hex(' ')}")
    # ---- compare the RAM image with the flash dump
    fw = IMG[0x2600:0x0D3200]
    diffs = []
    for i in range(0, min(len(ram), len(fw)), CHUNK):
        if ram[i:i + CHUNK] != fw[i:i + CHUNK]:
            diffs.append(i)
    print(f"\n  image vs flash: {len(diffs)} of {len(ram)//CHUNK} chunks differ")
    ranges = []
    for i in diffs:
        a = IMAGE[0] + i
        if ranges and a - ranges[-1][1] <= CHUNK:
            ranges[-1][1] = a + CHUNK
        else:
            ranges.append([a, a + CHUNK])
    for a, b in ranges[:20]:
        off = a - BASE
        print(f"    0x{a:08X}-0x{b:08X} ({b-a} B)  flash {IMG[off:off+8].hex(' ')}  ram {ram[a-IMAGE[0]:a-IMAGE[0]+8].hex(' ')}")
    report.update(image_bytes=len(ram), differing_ranges=[[a, b] for a, b in ranges],
                  state_files={k: hashlib.sha256(v).hexdigest() for k, v in state.items()})
    (OUT / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')

    # ---- structural validation of the typed model, from the live bytes
    F = {}
    for l in Path('/tmp/kc02-board-re/funcs_now.tsv').read_text().splitlines():
        fa, n, s = l.split('\t')
        F[int(fa, 16)] = n
    def codeptr(v):
        return 0x02000000 <= v < 0x020D0C00
    print("\n  === structural checks on live state ===")
    adc = struct.unpack_from('<24H', state['key_adc_table'], 0)
    okc = 0
    for k in range(6):
        kid, c, lo_, hi_ = adc[4*k:4*k+4]
        good = (lo_ == max(0, c - 0x23)) and (hi_ == min(0x400, c + 0x23))
        okc += good
        print(f"    adc[{k}] key_id=0x{kid:02X} centre={c} lo={lo_} hi={hi_} {'ok' if good else 'MISMATCH'}")
    ht = struct.unpack_from('<46I', state['handler_table'], 0)
    print(f"    dispatch table: {sum(1 for v in ht if v)} non-null, all code pointers: "
          f"{all(codeptr(v) for v in ht if v)}")
    ta = struct.unpack_from('<60I', state['task_array'], 0)
    ptrs = [v for v in ta if 0x020C0000 <= v < 0x02100000]
    print(f"    mode/keymap pointers in the mode table: {len(ptrs)} " +
          f"({', '.join(hex(p) for p in ptrs[:10])})")
    # each pointer should be a mode descriptor: label string + code ops + keymap
    good = 0
    for p in ptrs[:12]:
        label_ptr = struct.unpack_from('<I', IMG, p - BASE)[0]
        if not (0x02000000 <= label_ptr < 0x020D0C00):
            continue
        off = label_ptr - BASE
        s = IMG[off:IMG.find(b'\0', off, off + 40)].decode('gbk', 'replace')
        ops = struct.unpack_from('<3I', IMG, p + 8 - BASE)
        good += all(codeptr(o) for o in ops)
        print(f"      0x{p:08X}: label={s!r} ops={[hex(o) for o in ops]}")
    print(f"    descriptors with a readable label and three code ops: {good}/{min(12,len(ptrs))}")
    print("\n  audit written to", OUT)
    return 0


if __name__ == '__main__':
    sys.exit(main())
