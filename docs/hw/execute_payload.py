#!/usr/bin/env python3
"""Execute a small payload on the KC02, using the same safety envelope as the repo's
toolchain test harness (docs/RAM_MAP_MEASURED.md, tools/toolchain_tests/transport.py).

Sequence, all inside the measured-free window 0x020D8000..0x020D9EFF:
  1. verify the pinned firmware routine signatures (incl. the I-cache helper body)
  2. upload the 4-byte return-only canary + a known buffer and execute it; the buffer
     must come back unchanged (proves execution and the transfer path are intact)
  3. upload the dispatcher, the payload code and the 2 KiB packet, each verified by
     RAM read-back before anything executes
  4. run the stock I-cache reset (0x0202B014) -- required before executing new code
  5. execute through the dispatcher; the payload receives r3=packet, r4=length,
     r5=parameter and its results come back in the same 2 KiB transfer

Writes are whitelisted byte-for-byte; reads are limited to pinned sites or the
declared blobs; no other callback is accepted.
"""
import argparse, hashlib, json, struct, sys, datetime
from pathlib import Path

REPO = Path('/home/jbaiter/.src/kc02-fw-mod')
sys.path.insert(0, str(REPO / 'tools'))
import dump_firmware as dump
from toolchain_tests import cases, cache

HW = Path('/tmp/kc02-board-re/hw')
SHIM, CANARY, CODE = cases.SHIM, cases.CANARY, cases.CODE_ADDR
CODE_LIMIT, BASE, SIZE = cases.CODE_LIMIT, cases.BASE, cases.SIZE
INPUT, HEADER, OUTPUT = cases.INPUT, cases.HEADER, cases.OUTPUT
MAGIC = cases.MAGIC
DISPATCH = cases.DISPATCH
ICACHE_INIT_FN, ICACHE_SITE = cache.ICACHE_INIT_FN, cache.ICACHE_SITE


class PayloadTransport(dump.CameraTransport):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.firmware_ok = self.canary_ok = self.icache_ready = False
        self.blobs = {}
        self.verified = {}

    # ---- guard rails -------------------------------------------------------
    def check_firmware(self):
        dump.check_firmware(self)
        addr, expected, name = ICACHE_SITE
        if self.read_region(addr, len(expected)) != expected:
            raise dump.DumpError(f'Unsupported firmware: {name} body differs; refusing RAM execution.')
        self.firmware_ok = True

    def _approved_write(self, buffer, payload):
        for a, blob in self.blobs.items():
            off = buffer - a
            if 0 <= off < len(blob) and blob[off:off + len(payload)] == payload:
                return a
        return None

    def _approved_read(self, buffer, length):
        for a, blob in self.blobs.items():
            off = buffer - a
            if 0 <= off < len(blob) and length == min(SIZE, len(blob) - off):
                return a
        return None

    def read_region(self, address, length):
        data = b''
        while len(data) < length:
            data += self._command(address + len(data), min(SIZE, length - len(data)))
        return data

    def upload(self, address, blob, verify=True):
        for start in range(0, len(blob), SIZE):
            chunk = blob[start:start + SIZE]
            self._command(address + start, len(chunk), dump.CACHE_CLEAN_FN, payload=chunk)
        if verify and self.read_region(address, len(blob)) != blob:
            raise dump.DumpError(f'RAM read-back mismatch at 0x{address:08X}; refusing to execute.')
        self.verified[address] = blob

    def _command(self, buffer, length, callback=dump.CALLBACK_SKIP, offset=0, payload=None):
        if not self.claimed:
            raise dump.DumpError('USB interface has not been claimed.')
        if payload is not None:
            if not self.firmware_ok:
                raise dump.DumpError('Firmware signatures not checked.')
            if not 0 < length <= SIZE or callback != dump.CACHE_CLEAN_FN or offset:
                raise dump.DumpError('Invalid RAM write.')
            written = self._approved_write(buffer, payload)
            if written is None:
                raise dump.DumpError(f'Write to 0x{buffer:08X} is not an approved blob.')
            self.verified.pop(written, None)
            self.icache_ready = False
        elif callback == dump.CALLBACK_SKIP:
            sites = {(a, len(b)) for a, b, _ in (*dump.ROUTINE_SIGNATURES, ICACHE_SITE)}
            if offset or ((buffer, length) not in sites and self._approved_read(buffer, length) is None):
                raise dump.DumpError(f'Read 0x{buffer:08X}+{length} outside approved sites.')
        elif callback == ICACHE_INIT_FN:
            if not self.firmware_ok or buffer != BASE or length != SIZE or offset:
                raise dump.DumpError('Invalid instruction-cache maintenance request.')
            if self.verified.get(BASE) != self.blobs.get(BASE):
                raise dump.DumpError('Packet blob not verified; refusing I-cache maintenance.')
        elif callback in (CANARY, SHIM):
            if not self.firmware_ok or buffer != BASE or length != SIZE or offset:
                raise dump.DumpError('Invalid execution request.')
            if self.verified.get(callback) is None or self.verified.get(BASE) != self.blobs.get(BASE):
                raise dump.DumpError('Code/input blobs not verified; refusing execution.')
            if callback == CANARY and not self.icache_ready:
                raise dump.DumpError('I-cache not prepared for newly written canary code.')
            if callback == SHIM and not (self.canary_ok and self.icache_ready):
                raise dump.DumpError('Canary/I-cache prerequisites not met; refusing execution.')
        else:
            raise dump.DumpError(f'Callback 0x{callback:08X} is not permitted.')
        if payload is None and buffer == BASE and length != SIZE:
            raise dump.DumpError('Packet transfers must be exactly %d bytes.' % SIZE)
        self.tag = (self.tag + 1) & 0xFFFFFFFF
        direction = 0x80 if payload is None else 0x00
        cdb = bytes([0xCD]) + struct.pack('<III', dump.MEMORY_TRANSFER_FN, buffer, callback) + offset.to_bytes(3, 'little')
        cbw = struct.pack('<4sIIBBB', b'USBC', self.tag, length, direction, 0, 16) + cdb
        self._write(cbw)
        if payload is None:
            data = self._read_exact(length)
        else:
            self._write(payload)
            data = b''
        sig, tag, residue, status = struct.unpack('<4sIIB', self._read_exact(13))
        if (sig, tag, residue, status) != (b'USBS', self.tag, 0, 0):
            raise dump.DumpError(f'Invalid USB status {sig!r} tag={tag} residue={residue} status={status}; '
                                 'stop and power-cycle before retrying.')
        if callback in (SHIM, CANARY):
            self.verified.pop(BASE, None)      # another execution needs a fresh verified upload
            self.verified.pop(callback, None)
        return data


def build_packet(seed=0, mode=None, param=0, read_addr=0, words=None):
    pkt = bytearray(cases.PAD)
    if words is not None:
        for i, w in enumerate(words[:8]):
            struct.pack_into('<I', pkt, INPUT + 4 * i, w)
    else:
        struct.pack_into('<III', pkt, INPUT, seed, param, read_addr)
        struct.pack_into('<III', pkt, INPUT + 12, mode if mode is not None else seed, 0, 0)
    return bytes(pkt)


def parse_reply(data):
    h = struct.unpack_from('<8I', data, HEADER)
    out = struct.unpack_from('<26I', data, OUTPUT)
    words = {f'out[{i}]': out[i] for i in range(26)}
    return dict(magic_ok=(h[0] == MAGIC), test_id=h[1], echo_in=h[2], length=h[3],
                parameter=h[4], count=h[5], buffer=h[6], in1=h[7], output=list(out),
                mode_req=out[0], elapsed_ticks=out[1], read_addr=out[2],
                read_words=out[3:23], app_context0=out[23], dispatch0=out[24], dispatch1=out[25],
                words=words)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--payload', choices=['echo', 'mode', 'camprobe', 'camprobe2', 'sensorprobe'], required=True)
    ap.add_argument('--input-words', default=None, help='comma-separated u32 input words (override)')
    ap.add_argument('--mode', type=lambda v: int(v, 0), default=None)
    ap.add_argument('--param', type=lambda v: int(v, 0), default=0)
    ap.add_argument('--read-addr', type=lambda v: int(v, 0), default=0)
    ap.add_argument('--seed', type=lambda v: int(v, 0), default=0x11223344)
    ap.add_argument('--upload-only', action='store_true', help='stop after verified upload + I-cache reset')
    a = ap.parse_args()

    code = (HW / 'payloads' / f'{a.payload}.bin').read_bytes()
    if len(code) > CODE_LIMIT:
        raise SystemExit(f'payload is {len(code)} bytes, over the {CODE_LIMIT}-byte window')
    words = [int(w, 0) for w in a.input_words.split(',')] if a.input_words else None
    packet = build_packet(a.seed, a.mode, a.param, a.read_addr, words)
    report = dict(payload=a.payload, code_bytes=len(code), code_sha256=hashlib.sha256(code).hexdigest(),
                  packet_sha256=hashlib.sha256(packet).hexdigest(), mode=a.mode, param=a.param,
                  read_addr=a.read_addr, started=datetime.datetime.now(datetime.timezone.utc).isoformat())
    usb, backend = dump.load_usb()
    cams, webcams = dump.find_cameras(usb, backend)
    dev = dump.select_camera(cams, webcams, 1)
    report['device'] = dump.device_label(dev)
    try:
        with PayloadTransport(usb, dev, 5000) as t:
            t.check_firmware()
            print('  1. firmware signatures verified (incl. I-cache helper body)')
            t.blobs = {CANARY: dump.CANARY_BYTES, BASE: dump.CANARY_PATTERN}
            t.upload(CANARY, dump.CANARY_BYTES)
            t.upload(BASE, dump.CANARY_PATTERN)
            back = t._command(BASE, SIZE, ICACHE_INIT_FN)   # canary is new code: reset I-cache first
            if back != dump.CANARY_PATTERN:
                raise dump.DumpError('I-cache helper changed the canary buffer; stop and power-cycle')
            t.icache_ready = True
            back = t._command(BASE, SIZE, CANARY)
            if back != dump.CANARY_PATTERN:
                raise dump.DumpError('canary changed its buffer; stop and power-cycle')
            t.canary_ok = True
            print('  2. canary executed and returned its buffer unchanged')
            t.blobs = {CANARY: dump.CANARY_BYTES, SHIM: DISPATCH, CODE: code, BASE: packet}
            t.upload(SHIM, DISPATCH)
            t.upload(CODE, code)
            t.upload(BASE, packet)
            print(f'  3. uploaded + verified: shim 4 B, code {len(code)} B @0x{CODE:08X}, packet {len(packet)} B @0x{BASE:08X}')
            back = t._command(BASE, SIZE, ICACHE_INIT_FN)
            if back != packet:
                raise dump.DumpError('I-cache helper changed the packet; stop and power-cycle')
            t.icache_ready = True
            print('  4. stock I-cache reset executed')
            if a.upload_only:
                report['status'] = 'upload-only-ok'
            else:
                reply = t._command(BASE, SIZE, SHIM)
                parsed = parse_reply(reply)
                report['reply'] = parsed
                report['status'] = 'executed' if parsed['magic_ok'] else 'executed-without-magic'
                print(f"  5. payload executed: magic_ok={parsed['magic_ok']} test_id=0x{parsed['test_id']:08X}")
                print(f"     mode requested={parsed['mode_req']} elapsed={parsed['elapsed_ticks']} ticks "
                      f"(delay semantics from XOSTimeGet)")
                print(f"     app_context[0]=0x{parsed['app_context0']:08X}  dispatch[0]=0x{parsed['dispatch0']:08X} "
                      f"dispatch[1]=0x{parsed['dispatch1']:08X}")
                print("     words: " + ' '.join(f'{v:08x}' for v in parsed['output'][:26]))
                if parsed['read_addr']:
                    print(f"     read 20 words @0x{parsed['read_addr']:08X}: " +
                          ' '.join(f'{v:08x}' for v in parsed['read_words']))
    except Exception as exc:
        report['status'] = 'fail'
        report['error'] = f'{type(exc).__name__}: {exc}'
        print(f'  FAILED: {report["error"]}')
    finally:
        (HW / f'run_{a.payload}_{a.mode}.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report.get('status', '').startswith(('executed', 'upload-only')) else 1


if __name__ == '__main__':
    sys.exit(main())
