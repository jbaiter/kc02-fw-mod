#!/usr/bin/env python3
"""Addresses and lengths in the region table are firmware evidence, not assumptions: run
docs/hw/scan_ram_access.py against the listing to see which addresses the code actually
materialises, their access width and their site counts. Never pad a region to cover a
neighbouring global.

Read-only observer for the KC02's live state.

Addresses and lengths in this table are firmware evidence, not assumptions: run
docs/hw/scan_ram_access.py against the listing to see which addresses the code materialises,
their access width and site counts. Regions must not be padded to cover a neighbouring global.


Uses the repo's stock transport exactly like tools/survey_ram.py: the stock
memory-transfer command with CALLBACK_SKIP (no RAM code runs, no writes).  The only
deviation is an explicit, declared region list instead of a blanket window, so that
runtime state living below 0x020C6000 (keypad/ADC tables in .data) can be read too.
Every region is bounds-checked against that list before a transfer is issued.
"""
import datetime, json, hashlib, struct, sys
from pathlib import Path

REPO = Path('/home/jbaiter/.src/kc02-fw-mod')
sys.path.insert(0, str(REPO / 'tools'))
import dump_firmware as dump

OUT = Path('/tmp/kc02-board-re/hw/snapshot01')
CHUNK = 512

# (name, address, length) — all dynamic state, no code, no patch regions
REGIONS = [
    # name, address, length. Every length here is firmware evidence, not an assumed struct:
    # docs/hw/scan_ram_access.py reports which addresses the code actually materialises, their
    # access width and their site counts. Do NOT pad a region to cover a neighbour just because
    # it is adjacent; add a separate entry instead. Note the scan only sees absolute address
    # materialisation (movhi/ori), so fields reached as base+offset are not attributed.
    ("handler_table",   0x020CCC8C, 0x0B8),   # msgFuncRegister fills [msgId] = handler
    ("task_array",      0x020CCD5C, 12 * 20), # kc02_sys_task descriptors
    ("mode_byte",       0x020CCF04, 0x20),    # game/select byte
    ("game_field",      0x020CD0CC, 0x40),    # 8x8 playfield
    ("game_state",      0x020CD69C, 0x40),
    ("key_adc_table",   0x020C268C, 6 * 8),   # adc_key_table (runtime windows)
    ("device_list",     0x020C259C, 9 * 0x18),
    ("keypad_state",    0x020C87A4, 0x20),
    ("keypad_state2",   0x020C8780, 0x20),
    ("app_context",     0x020D416C, 0x74),    # ends before ui_queue at +0x74
    ("sensor_item",     0x020CB3F0, 0x50),
    ("print_sensor",    0x020D2EAC, 4),       # four byte globals, 0x020D2EAC..0xAF
    ("printer_counter", 0x020D2EC8, 4),       # one word; sole access is a store in print_run_A
    ("ram_020d2edc",    0x020D2EDC, 0x0C),    # three word globals; values resemble an SFAT descriptor
    ("ui_queue",        0x020D41E0, 0x30),
    ("mailbox",         0x020DB300, 0x100),
]

class ObserverTransport(dump.CameraTransport):
    def _command(self, buffer, length, callback=dump.CALLBACK_SKIP, offset=0, payload=None):
        if not self.claimed:
            raise dump.DumpError("USB interface has not been claimed.")
        if payload is not None:
            raise dump.DumpError("observer is read-only: RAM writes are not permitted.")
        if callback != dump.CALLBACK_SKIP or offset:
            raise dump.DumpError("observer must not execute RAM code.")
        pinned = {(a, len(b)) for a, b, _ in dump.ROUTINE_SIGNATURES}
        declared = any(buffer >= a and buffer + length <= a + l for _, a, l in REGIONS)
        if (buffer, length) not in pinned and not declared:
            raise dump.DumpError(f"read 0x{buffer:08X}+{length} is not in a declared region")
        self.tag = (self.tag + 1) & 0xFFFFFFFF
        cdb = bytes([0xCD]) + struct.pack("<III", dump.MEMORY_TRANSFER_FN, buffer, callback)
        cbw = struct.pack("<4sIIBBB", b"USBC", self.tag, length, 0x80, 0, 16) + cdb + b"\0\0\0"
        self._write(cbw)
        data = self._read_exact(length)
        csw = self._read_exact(13)
        sig, tag, residue, status = struct.unpack("<4sIIB", csw)
        if sig != b"USBS" or tag != self.tag or residue != 0 or status != 0:
            raise dump.DumpError(f"bad USB status sig={sig!r} tag={tag} residue={residue} status={status}; power-cycle before retrying")
        return data

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    usb, backend = dump.load_usb()
    cams, webcams = dump.find_cameras(usb, backend)
    dev = dump.select_camera(cams, webcams, 1)
    rep = dict(device=dump.device_label(dev), started=datetime.datetime.now(datetime.timezone.utc).isoformat())
    blobs, regions = {}, []
    try:
        with ObserverTransport(usb, dev, 5000) as d:
            dump.check_firmware(d)
            rep["firmware_signatures_ok"] = True
            for name, addr, ln in REGIONS:
                data = b""
                for off in range(0, ln, CHUNK):
                    n = min(CHUNK, ln - off)
                    data += d._command(addr + off, n)
                blobs[name] = data
                regions.append(dict(name=name, address=addr, length=ln, sha256=hashlib.sha256(data).hexdigest()))
                print(f"  read {name:<14} 0x{addr:08X}+{ln:<4} {data[:16].hex(' ')}")
            rep["status"] = "pass"
    except Exception as exc:
        rep["status"] = "fail"; rep["error"] = f"{type(exc).__name__}: {exc}"
        print(f"  FAILED: {rep['error']}")
    finally:
        for name, data in blobs.items():
            (OUT / f"{name}.bin").write_bytes(data)
        rep["regions"] = regions
        (OUT / "snapshot.json").write_text(json.dumps(rep, indent=2) + "\n")
    return 0 if rep["status"] == "pass" else 1

if __name__ == '__main__':
    sys.exit(main())
