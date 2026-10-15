#!/usr/bin/env python3
"""Bounded stock-preview bench run under one continuously claimed USB interface.

No flash/SD writes, no set_configuration, no intermediate kernel-driver rebind.
Reuses only the exact known RAM hoist or installs it at its allowlisted sites.
Executes small verified probes; captures measured DMA buffers read-only. Returns
from preview to the original USB mode on SUCCESS. Stops on the first USB error.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import struct
import time

import execute_payload as hw
import capture_preview as capture
import kc02_unlock as unlock
import kc02_usb as usb_guard


class SessionTransport(hw.PayloadTransport):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.read_only_regions = [(0x02000404, 4), (0x020d9200, 48),
                                  (0x020ccd44, 24), (0x020ccdcc, 8)]

    def _approved_read(self, buffer, length):
        approved = super()._approved_read(buffer, length)
        if approved is not None:
            return approved
        if 0 < length <= hw.SIZE and any(a <= buffer and buffer + length <= a + size
                                         for a, size in self.read_only_regions):
            return buffer
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--payload-dir', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True, help='new evidence directory')
    ap.add_argument('--observe-seconds', type=int, default=30)
    ap.add_argument('--warmup-seconds', type=int, default=0,
                    help='bounded exposure/scene settling after camera entry')
    args = ap.parse_args()
    if not 0 <= args.observe_seconds <= 120 or not 0 <= args.warmup_seconds <= 30:
        ap.error('--observe-seconds must be 0..120; --warmup-seconds must be 0..30')
    manifest = json.loads((args.payload_dir / 'manifest.json').read_text())
    codes = {}
    for name in ('mode', 'sprprobe', 'powerprobe', 'packetcheck'):
        code = (args.payload_dir / (name + '.bin')).read_bytes()
        if not 0 < len(code) <= hw.CODE_LIMIT or hashlib.sha256(code).hexdigest() != manifest[name]['sha256']:
            raise ValueError('invalid code/manifest: ' + name)
        codes[name] = code
    args.output.mkdir(parents=True, exist_ok=False)
    report = dict(started=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  driver_handoffs_during_measurement=0, steps=[], atomic_capture=False)
    try:
        usb, backend = hw.dump.load_usb()
        cams, webcams = hw.dump.find_cameras(usb, backend)
        dev = hw.dump.select_camera(cams, webcams, 1)
        report['device'] = hw.dump.device_label(dev)
        initial_identity = (dev.bus, dev.address)
        return_pending = False
        with SessionTransport(usb, dev, 5000, verbose=False) as transport:
            transport.check_firmware()
            def state():
                values = struct.unpack('<6I', transport.read_region(0x020ccd44, 24))
                idle = struct.unpack('<2I', transport.read_region(0x020ccdcc, 8))
                return dict(countdown_44=values[0], countdown_48=values[1], previous=values[2],
                            pending=values[3], mode=values[4], descriptor=values[5],
                            idle_accumulator=idle[0], idle_tick=idle[1])
            initial = state()
            report['initial_state'] = initial
            if initial['mode'] not in (3, 10):
                raise hw.dump.DumpError('expected camera or stock USB mode')
            expected = unlock.hoist_plan(keep_awake=True)
            hook = transport.read_region(unlock.PER_FRAME_HOOK_ADDR, 4)
            if hook == unlock.CODE_PATCH_BYTES:
                stub = transport.read_region(unlock.HOIST_STUB_ADDR, 48)
                if stub != expected[0][2]:
                    raise hw.dump.DumpError('unknown active hoist; refusing to rewrite running code')
                report['hoist'] = 'existing exact 48-byte bench hoist reused'
            elif hook == unlock.PER_FRAME_HOOK_STOCK:
                transport.blobs = {address: blob for _, address, blob in expected}
                for _, address, blob in expected:
                    usb_guard._require_poke_write(address, len(blob))
                    usb_guard._require_poke_payload(blob)
                    transport.upload(address, blob)
                report['hoist'] = 'installed 48-byte bench hoist at allowlisted RAM/code sites'
            else:
                raise hw.dump.DumpError('unknown hook instruction; refusing writes')
            print(report['hoist'], flush=True)

            def probe(label, name, words):
                folder = args.output / label
                folder.mkdir()
                packet = hw.build_packet(words=words)
                (folder / 'code.bin').write_bytes(codes[name])
                (folder / 'input.bin').write_bytes(packet)
                entry = dict(payload=name, input_words=words,
                             code_sha256=manifest[name]['sha256'],
                             started=datetime.datetime.now(datetime.timezone.utc).isoformat())
                report['steps'].append(dict(label=label, **entry))
                try:
                    reply = hw.execute_verified(transport, codes[name], packet)
                    (folder / 'reply.bin').write_bytes(reply)
                    parsed = hw.parse_reply(reply)
                    entry['reply'] = parsed
                    if not (parsed['magic_ok'] and parsed['test_id'] == manifest[name]['test_id'] and
                            parsed['length'] == hw.SIZE and parsed['buffer'] == hw.BASE and
                            parsed['parameter'] == 0 and parsed['echo_in'] == words[0]):
                        raise hw.dump.DumpError('reply identity/header mismatch')
                    if name == 'packetcheck':
                        checksum = 2166136261
                        for byte in packet:
                            checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
                        if parsed['output'][:2] != [checksum, hw.SIZE]:
                            raise hw.dump.DumpError('CPU packet hash mismatch')
                    if name == 'sprprobe':
                        out = parsed['output']
                        if out[:2] != [words[0], 12]:
                            raise hw.dump.DumpError('SPR bank/count mismatch')
                        parsed['registers'] = {f'0x{out[2+2*i]:04x}': out[3+2*i] for i in range(12)}
                    entry['status'] = 'executed'
                    print(f'{label}: PASS; outputs={parsed["output"][:12]}', flush=True)
                    return parsed
                except Exception as exc:
                    entry.update(status='fail', error=f'{type(exc).__name__}: {exc}')
                    raise
                finally:
                    (folder / 'report.json').write_text(json.dumps(entry, indent=2) + '\n')

            probe('packet-check', 'packetcheck', [0])
            probe('power-before', 'powerprobe', [0])
            if initial['mode'] == 10:
                probe('request-camera', 'mode', [3, 0])
            deadline = time.monotonic() + 5
            while state()['mode'] != 3:
                if time.monotonic() >= deadline:
                    raise hw.dump.DumpError('camera mode did not arrive')
                time.sleep(0.05)
            probe('power-camera', 'powerprobe', [0])
            warmup_end = time.monotonic() + args.warmup_seconds
            while time.monotonic() < warmup_end:
                if state()['mode'] != 3:
                    raise hw.dump.DumpError('unexpected mode during exposure warmup')
                time.sleep(min(0.2, max(0, warmup_end - time.monotonic())))
            probe('csi', 'sprprobe', [3])
            probe('de', 'sprprobe', [0])
            probe('panel', 'sprprobe', [1])
            probe('rotate', 'sprprobe', [5])
            layouts = {name: capture.layout(args.output / name / 'report.json', name == 'csi')
                       for name in ('csi', 'de')}
            for name, info in layouts.items():
                for plane in ('y', 'uv'):
                    transport.read_only_regions.append((info[plane], info[plane + '_length']))
                hashes = []
                for index in range(2):
                    planes = {}
                    for plane in ('y', 'uv'):
                        data = transport.read_region(info[plane], info[plane + '_length'])
                        (args.output / f'{name}-{index}.{plane}.bin').write_bytes(data)
                        planes[plane] = data
                    rgb = capture.ppm(planes['y'], planes['uv'], info)
                    (args.output / f'{name}-{index}.ppm').write_bytes(rgb)
                    hashes.append(hashlib.sha256(planes['y']).hexdigest())
                    # Bounded settle so a later frame can differ from an earlier
                    # one; distinguishes a live plane from a frozen snapshot.
                    if index == 0:
                        time.sleep(0.4)
                info['luma_hashes'] = hashes
                info['luma_changed'] = hashes[0] != hashes[1]
                print(f'{name}: captured two {info["width"]}x{info["height"]} frames; '
                      f'luma_changed={info["luma_changed"]}', flush=True)
            report['layouts'] = layouts
            report['heartbeats'] = []
            deadline = time.monotonic() + args.observe_seconds
            while True:
                current = dict(at=datetime.datetime.now(datetime.timezone.utc).isoformat(), **state())
                report['heartbeats'].append(current)
                (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
                if current['mode'] != 3:
                    raise hw.dump.DumpError('unexpected mode change during observation')
                if time.monotonic() >= deadline:
                    break
                time.sleep(min(1, max(0, deadline - time.monotonic())))
            probe('power-after', 'powerprobe', [0])
            report['preview_final_state'] = state()
            if initial['mode'] == 10:
                probe('return-usb', 'mode', [10, 1])
                # Hardware mode-10 entry reinitializes USB and re-enumerates.
                # Do not send another command to the departing handle or replay
                # the mode request. Reconcile through a NEW identity below.
                return_pending = True
            else:
                report['final_state'] = state()
        if return_pending:
            deadline = time.monotonic() + 12
            while True:
                cams, webcams = hw.dump.find_cameras(usb, backend)
                candidates = [camera for camera in cams
                              if (camera.bus, camera.address) != initial_identity]
                if candidates:
                    if len(candidates) != 1:
                        raise hw.dump.DumpError('ambiguous camera identity after re-enumeration')
                    new = candidates[0]
                    with SessionTransport(usb, new, 5000) as transport:
                        transport.check_firmware()
                        values = struct.unpack('<6I', transport.read_region(0x020ccd44, 24))
                        idle = struct.unpack('<2I', transport.read_region(0x020ccdcc, 8))
                        if values[4] != 10 or idle[1] < report['preview_final_state']['idle_tick']:
                            raise hw.dump.DumpError('USB return did not preserve mode/uptime')
                        report['final_state'] = dict(mode=values[4], idle_accumulator=idle[0],
                                                    idle_tick=idle[1], pending=values[3])
                        report['reenumerated_device'] = hw.dump.device_label(new)
                    break
                if time.monotonic() >= deadline:
                    raise hw.dump.DumpError('return request accepted, but USB re-enumeration not observed')
                time.sleep(0.1)
        report['status'] = 'pass'
    except Exception as exc:
        report.update(status='fail', error=f'{type(exc).__name__}: {exc}')
        print(report['error'], flush=True)
    finally:
        report['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
