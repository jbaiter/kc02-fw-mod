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

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools'))
import dump_firmware as dump
import kc02_unlock as unlock
from toolchain_tests import cases, cache

# A RAM hoist rewrites a live per-frame instruction; reuse the single source of
# truth from kc02_unlock rather than restating the bytes here.
FRAME_HOOK_ADDR = unlock.PER_FRAME_HOOK_ADDR
FRAME_HOOK_STOCK = unlock.PER_FRAME_HOOK_STOCK
HOIST_PATCH_BYTES = unlock.CODE_PATCH_BYTES

PAYLOADS = Path(__file__).resolve().parent / 'payloads'
SHIM, CANARY, CODE = cases.SHIM, cases.CANARY, cases.CODE_ADDR
# Preview writes a live scalar at 0x020D9C00 inside the old toolchain packet.
# Keep the packet below UI stubs, and code below the packet. Survey/evidence:
# docs/hw/2026-10-15.md. This is a bench layout, not a larger RAM reservation.
BASE, SIZE = 0x020D8800, cases.SIZE
CODE_LIMIT = min(cases.CODE_LIMIT, BASE - CODE)
INPUT, HEADER, OUTPUT = cases.INPUT, cases.HEADER, cases.OUTPUT
MAGIC = cases.MAGIC
DISPATCH = cases.DISPATCH
ICACHE_INIT_FN, ICACHE_SITE = cache.ICACHE_INIT_FN, cache.ICACHE_SITE
# Reviewed in canonical Ghidra: CS low, 0x9f, three reads, CS high, return.
FLASH_ID_SITE = (0x0203D1FC, bytes.fromhex(
    'fc4fe1d7f817e1d7f40fe1d70000609cf0ff219c32c1ff079f00609c'
    'aac0ff07bec0ff0708004bb8bcc0ff07005862e108004bb8b9c0ff07'
    '0100609c005862e1005801d426c1ff07000061851000219cfcff2185'
    'f4ff2184f8ff418400480044'), 'stock read-only JEDEC ID helper')


class PayloadTransport(dump.CameraTransport):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.firmware_ok = self.canary_ok = self.icache_ready = False
        self.hoist_active = None
        self.blobs = {}
        self.verified = {}

    # ---- guard rails -------------------------------------------------------
    def check_firmware(self):
        dump.check_firmware(self)
        for addr, expected, name in (ICACHE_SITE, FLASH_ID_SITE):
            if self.read_region(addr, len(expected)) != expected:
                raise dump.DumpError(f'Unsupported firmware: {name} body differs; refusing RAM execution.')
        # A RAM hoist rewrites a live per-frame instruction. Payload runs must not
        # silently stack on top of an unknown or stale one; report the exact state.
        hook = self.read_region(FRAME_HOOK_ADDR, 4)
        if hook == HOIST_PATCH_BYTES:
            self.hoist_active = True
            print('  frame hook: known bench hoist already active (left in place)', flush=True)
        elif hook == FRAME_HOOK_STOCK:
            self.hoist_active = False
        else:
            raise dump.DumpError('Unknown per-frame hook %s at 0x%08x; power-cycle to '
                                 'stock firmware before running payloads.'
                                 % (hook.hex(), FRAME_HOOK_ADDR))
        self.firmware_ok = True

    def _approved_write(self, buffer, payload):
        for a, blob in self.blobs.items():
            off = buffer - a
            if 0 <= off < len(blob) and blob[off:off + len(payload)] == payload:
                return a
        return None

    def _approved_read(self, buffer, length):
        if buffer == FRAME_HOOK_ADDR and length == 4:
            return buffer
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
            sites = {(a, len(b)) for a, b, _ in (*dump.ROUTINE_SIGNATURES, ICACHE_SITE, FLASH_ID_SITE)}
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


def execute_verified(t, code, packet, upload_only=False):
    """Execute on an already claimed transport, without driver hand-offs.

    Every invocation repeats the canary and upload/cache checks. The caller owns
    the USB session and persistent evidence. None of these paths writes flash.
    """
    if not t.firmware_ok or not 0 < len(code) <= CODE_LIMIT or len(packet) != SIZE:
        raise dump.DumpError('Invalid/unverified execution inputs.')
    t.canary_ok = t.icache_ready = False
    t.verified = {}
    t.blobs = {CANARY: dump.CANARY_BYTES, BASE: dump.CANARY_PATTERN}
    t.upload(CANARY, dump.CANARY_BYTES)
    t.upload(BASE, dump.CANARY_PATTERN)
    if t._command(BASE, SIZE, ICACHE_INIT_FN) != dump.CANARY_PATTERN:
        raise dump.DumpError('I-cache helper changed the canary buffer; stop and power-cycle')
    t.icache_ready = True
    if t._command(BASE, SIZE, CANARY) != dump.CANARY_PATTERN:
        raise dump.DumpError('canary changed its buffer; stop and power-cycle')
    t.canary_ok = True
    print('  canary executed and returned its buffer unchanged', flush=True)
    t.blobs = {CANARY: dump.CANARY_BYTES, SHIM: DISPATCH, CODE: code, BASE: packet}
    t.upload(SHIM, DISPATCH)
    t.upload(CODE, code)
    t.upload(BASE, packet)
    print(f'  uploaded + verified: code {len(code)} B @0x{CODE:08X}, '
          f'packet {SIZE} B @0x{BASE:08X}', flush=True)
    if t._command(BASE, SIZE, ICACHE_INIT_FN) != packet:
        raise dump.DumpError('I-cache helper changed the packet; stop and power-cycle')
    t.icache_ready = True
    return None if upload_only else t._command(BASE, SIZE, SHIM)


def build_packet(seed=0, mode=None, param=0, read_addr=0, words=None):
    pkt = bytearray(cases.PAD)
    # Inputs end at HEADER, not OUTPUT: 16 words, with omitted options zeroed.
    # Padding bytes must never become implicit feature/write permissions.
    pkt[INPUT:HEADER] = bytes(HEADER-INPUT)
    if words is not None:
        if len(words) > (HEADER-INPUT)//4:
            raise ValueError('payload input would overlap its reply header')
        for i, w in enumerate(words):
            struct.pack_into('<I', pkt, INPUT + 4 * i, w)
    else:
        struct.pack_into('<III', pkt, INPUT, seed, param, read_addr)
        struct.pack_into('<III', pkt, INPUT + 12, mode if mode is not None else seed, 0, 0)
    return bytes(pkt)


def parse_reply(data):
    h = struct.unpack_from('<8I', data, HEADER)
    # Trust the header's own word count rather than a hardcoded 26, so a payload
    # that reports more fields is not silently truncated.
    nout = h[5] if 26 < h[5] <= 40 else 26
    out = struct.unpack_from(f'<{nout}I', data, OUTPUT)
    words = {f'out[{i}]': out[i] for i in range(nout)}
    return dict(magic_ok=(h[0] == MAGIC), test_id=h[1], echo_in=h[2], length=h[3],
                parameter=h[4], count=h[5], buffer=h[6], in1=h[7], output=list(out),
                mode_req=out[0], elapsed_ticks=out[1], read_addr=out[2],
                read_words=out[3:23], app_context0=out[23], dispatch0=out[24], dispatch1=out[25],
                words=words)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--payload', choices=['echo', 'mode', 'camprobe', 'camprobe2', 'sensorprobe', 'sprprobe', 'adcprobe', 'timerprobe', 'flashidprobe', 'packetcheck', 'powerprobe', 'displayprobe', 'sensoridprobe', 'sensorclkprobe', 'sensorbusprobe', 'iic1probe', 'ramalloc', 'softiicprobe', 'gpiomapprobe', 'ramfree'], required=True)
    ap.add_argument('--input-words', default=None, help='comma-separated u32 input words (override)')
    ap.add_argument('--mode', type=lambda v: int(v, 0), default=None)
    ap.add_argument('--param', type=lambda v: int(v, 0), default=0)
    ap.add_argument('--read-addr', type=lambda v: int(v, 0), default=0)
    ap.add_argument('--seed', type=lambda v: int(v, 0), default=0x11223344)
    ap.add_argument('--upload-only', action='store_true', help='stop after verified upload + I-cache reset')
    ap.add_argument('--payload-dir', type=Path, default=PAYLOADS)
    ap.add_argument('--output-dir', type=Path, help='new evidence directory (refuses existing paths)')
    a = ap.parse_args()
    if a.payload in ('camprobe', 'camprobe2') and a.input_words is None:
        ap.error('camera probes require explicit --input-words mode,param,address,wait_ms; '
                 'the generic seed defaults are not safe for these payloads')
    words = [int(w, 0) for w in a.input_words.split(',')] if a.input_words else None
    if words is not None and (not 1 <= len(words) <= 8 or any(not 0 <= w <= 0xFFFFFFFF for w in words)):
        ap.error('--input-words requires 1..8 unsigned 32-bit words')
    if a.payload in ('camprobe', 'camprobe2') and (len(words) != 4 or words[3] > 500):
        ap.error('camera probes require four words and a wait of at most 500 ms')
    if a.payload == 'mode' and (words is None or len(words) != 2 or words[0] not in (3, 6, 10)):
        ap.error('bench mode requests require --input-words mode,param; mode must be '
                 '3 (camera), 6 (menu) or 10 (USB), never power-off')

    code = (a.payload_dir / f'{a.payload}.bin').read_bytes()
    if len(code) > CODE_LIMIT:
        raise SystemExit(f'payload is {len(code)} bytes, over the {CODE_LIMIT}-byte window')
    packet = build_packet(a.seed, a.mode, a.param, a.read_addr, words)
    now = datetime.datetime.now().astimezone()
    outdir = a.output_dir or (Path.home() / 'kc02-hw' / now.strftime('%F') /
                             now.strftime('payload-%H%M%S-%f'))
    outdir.mkdir(parents=True, exist_ok=False)
    (outdir / 'code.bin').write_bytes(code)
    (outdir / 'input.bin').write_bytes(packet)
    report = dict(payload=a.payload, code_bytes=len(code), code_sha256=hashlib.sha256(code).hexdigest(),
                  packet_sha256=hashlib.sha256(packet).hexdigest(), mode=a.mode, param=a.param,
                  read_addr=a.read_addr, input_words=words, command=sys.argv,
                  started=datetime.datetime.now(datetime.timezone.utc).isoformat())
    try:
        usb, backend = dump.load_usb()
        cams, webcams = dump.find_cameras(usb, backend)
        dev = dump.select_camera(cams, webcams, 1)
        report['device'] = dump.device_label(dev)
        with PayloadTransport(usb, dev, 5000) as t:
            t.check_firmware()
            print('  firmware signatures verified (incl. I-cache and JEDEC helpers)')
            reply = execute_verified(t, code, packet, a.upload_only)
            if reply is None:
                report['status'] = 'upload-only-ok'
            else:
                (outdir / 'reply.bin').write_bytes(reply)
                parsed = parse_reply(reply)
                report['reply'] = parsed
                if not (parsed['magic_ok'] and parsed['length'] == SIZE and
                        parsed['buffer'] == BASE and parsed['parameter'] == a.param and
                        parsed['echo_in'] == struct.unpack_from('<I', packet, INPUT)[0]):
                    raise dump.DumpError('Execution reply header mismatch; stop and power-cycle.')
                report['status'] = 'executed'
                print(f"  5. payload executed: magic_ok={parsed['magic_ok']} test_id=0x{parsed['test_id']:08X}")
                print("     words: " + ' '.join(f'{v:08x}' for v in parsed['output']))
                if a.payload == 'sprprobe':
                    bank, count = parsed['output'][:2]
                    if count != 12 or bank != struct.unpack_from('<I', packet, INPUT)[0]:
                        raise dump.DumpError('Invalid SPR bank/count; refusing measurement.')
                    parsed['registers'] = {
                        f'0x{parsed["output"][2 + 2*i]:04x}': parsed['output'][3 + 2*i]
                        for i in range(count)
                    }
                    for selector, value in parsed['registers'].items():
                        print(f'     SPR {selector} = 0x{value:08x}')
                elif a.payload == 'sensorprobe':
                    print(f'     sensor ops=0x{parsed["output"][0]:08x} '
                          f'independent IIC byte=0x{parsed["output"][1]:02x}')
                elif a.payload == 'camprobe':
                    print(f"     mode requested={parsed['mode_req']} elapsed={parsed['elapsed_ticks']} ms")
    except Exception as exc:
        report['status'] = 'fail'
        report['error'] = f'{type(exc).__name__}: {exc}'
        print(f'  FAILED: {report["error"]}')
    finally:
        (outdir / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(f'  evidence: {outdir}')
    return 0 if report.get('status') in ('executed', 'upload-only-ok') else 1


if __name__ == '__main__':
    sys.exit(main())
