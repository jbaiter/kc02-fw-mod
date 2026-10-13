#!/usr/bin/env python3
"""Read-only sweep of .bss + live classification of the RAM globals inventory."""
import json, struct, sys, hashlib
from pathlib import Path
REPO = Path('/home/jbaiter/.src/kc02-fw-mod')
sys.path.insert(0, str(REPO / 'tools'))
import dump_firmware as dump
from toolchain_tests import cache
OUT = Path('/tmp/kc02-board-re/hw/audit02'); OUT.mkdir(parents=True, exist_ok=True)
CHUNK = 2048
BSS = (0x020D0C00, 0x02103980)      # rest of .bss up to the measured HAL pool start
POOL = (0x02103980, 0x02104980)     # pool start should still be untouched

class T(dump.CameraTransport):
    def _command(self, buffer, length, callback=dump.CALLBACK_SKIP, offset=0, payload=None):
        if not self.claimed: raise dump.DumpError('not claimed')
        if payload is not None or callback != dump.CALLBACK_SKIP or offset:
            raise dump.DumpError('read-only sweep')
        pinned = {(a, len(b)) for a, b, _ in (*dump.ROUTINE_SIGNATURES, cache.ICACHE_SITE)}
        if (buffer, length) not in pinned and not (BSS[0] <= buffer and buffer + length <= BSS[1]) \
           and not (POOL[0] <= buffer and buffer + length <= POOL[1]):
            raise dump.DumpError(f'read 0x{buffer:08X}+{length} outside the declared window')
        self.tag = (self.tag + 1) & 0xFFFFFFFF
        cdb = bytes([0xCD]) + struct.pack('<III', dump.MEMORY_TRANSFER_FN, buffer, callback)
        self._write(struct.pack('<4sIIBBB', b'USBC', self.tag, length, 0x80, 0, 16) + cdb + b'\0\0\0')
        data = self._read_exact(length)
        sig, tag, res, st = struct.unpack('<4sIIB', self._read_exact(13))
        if (sig, tag, res, st) != (b'USBS', self.tag, 0, 0): raise dump.DumpError('bad CSW')
        return data

usb, backend = dump.load_usb(); cams, webs = dump.find_cameras(usb, backend)
dev = dump.select_camera(cams, webs, 1)
with T(usb, dev, 5000) as t:
    dump.check_firmware(t)
    bss = bytearray()
    for a in range(BSS[0], BSS[1], CHUNK):
        bss += t._command(a, min(CHUNK, BSS[1] - a))
    pool = bytearray()
    for a in range(POOL[0], POOL[1], CHUNK):
        pool += t._command(a, min(CHUNK, POOL[1] - a))
(OUT / 'bss.bin').write_bytes(bytes(bss)); (OUT / 'pool_start.bin').write_bytes(bytes(pool))
nz = sum(1 for b in bss if b)
print(f"  .bss 0x{BSS[0]:08X}-0x{BSS[1]:08X}: {len(bss)} B, non-zero {nz} ({100*nz/len(bss):.1f}%)")
print(f"  HAL pool start 0x{POOL[0]:08X}: all zero = {all(b == 0 for b in pool)}")
glob = json.load(open('/tmp/kc02-board-re/ram_globals.json'))['globals']
addrs = []
for it in glob:
    if isinstance(it, dict):
        for k in ('addr', 'address', 'cpu', 'a', 'start'):
            if k in it: addrs.append(int(str(it[k]), 0)); break
    elif isinstance(it, (int, str)):
        try: addrs.append(int(str(it), 0))
        except ValueError: pass
cls = {'zero': 0, 'code_ptr': 0, 'ram_ptr': 0, 'small_int': 0, 'other': 0}
examples = {}
for a in addrs:
    if not (BSS[0] <= a < BSS[1]): continue
    v = struct.unpack_from('<I', bss, a - BSS[0])[0]
    if v == 0: k = 'zero'
    elif 0x02000000 <= v < 0x020D0C00: k = 'code_ptr'
    elif 0x020D0C00 <= v < 0x02800000: k = 'ram_ptr'
    elif v < 0x10000: k = 'small_int'
    else: k = 'other'
    cls[k] += 1; examples.setdefault(k, []).append((a, v))
print(f"  RAM globals inventory ({len(addrs)} entries) live classification: {cls}")
for k, v in examples.items():
    print(f"    {k}: " + ", ".join(f"0x{a:08X}=0x{x:08X}" for a, x in v[:5]))
(OUT / 'class.json').write_text(json.dumps({'bss': list(BSS), 'nonzero': nz, 'classes': cls,
    'examples': {k: [[a, x] for a, x in v[:20]] for k, v in examples.items()}}, indent=2) + '\n')
print("  written to", OUT)
