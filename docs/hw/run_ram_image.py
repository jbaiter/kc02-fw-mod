#!/usr/bin/env python3
"""Run Luma from RAM on the original KC02: bounded, verified, and reversible.

This is the hardware RAM-run path. It never touches flash and it is designed so
that even a total failure of the Luma code leaves the camera recoverable without
a power cycle.

Order of operations, and why:

  1. verify the pinned stock firmware signatures (same envelope as every other
     bench runner)
  2. read-only snapshot of the whole RAM window the run will occupy
  3. re-read samples to prove the window is stable, i.e. not being written by
     the running firmware
  4. upload the bench image, every 2 KiB chunk read back and compared
  5. execute Luma's bench-only entry, which runs the real hardware_init +
     app_render + present for exactly one frame and then RETURNS
  6. read the beacon and Luma's Y/UV planes straight out of RAM
  7. restore the snapshot and verify the restore by readback

Step 5 is bounded because the entry returns: the USB callback still completes,
so the host keeps control and no power cycle is needed to get the camera back.

The window is inside the stock firmware's allocator pool, which is why the
snapshot/restore steps are mandatory rather than optional.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import struct
import sys
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import execute_payload as hw
import dump_firmware as dump

CORE = REPO / 'custom-firmware'
CHUNK = hw.SIZE

# Never allow the window to overlap anything the harness itself owns.
FORBIDDEN = ((hw.CANARY, 4), (hw.SHIM, 0x100), (hw.CODE, hw.CODE_LIMIT), (hw.BASE, hw.SIZE))


class RamTransport(hw.PayloadTransport):
    """Same safety envelope, plus one explicitly declared RAM window."""

    def __init__(self, *a, window=(0, 0), **k):
        super().__init__(*a, **k)
        base, size = window
        if base <= 0 or size <= 0:
            raise ValueError('RAM window must be declared')
        for addr, length in FORBIDDEN:
            if base < addr + length and addr < base + size:
                raise ValueError(f'RAM window overlaps harness region 0x{addr:08X}')
        self.window = (base, size)

    def _in_window(self, buffer, length):
        base, size = self.window
        off = buffer - base
        return 0 <= off and length > 0 and off + length <= size

    def _approved_write(self, buffer, payload):
        if self._in_window(buffer, len(payload)):
            return buffer
        return super()._approved_write(buffer, payload)

    def _approved_read(self, buffer, length):
        if self._in_window(buffer, length):
            return buffer
        return super()._approved_read(buffer, length)


def yuv420_to_rgb(y, uv, width, height, pitch=None):
    """BT.601, matching the panel's own coefficients closely enough to review."""
    if pitch is None:
        pitch = width
    out = bytearray(width * height * 3)
    for row in range(height):
        for col in range(width):
            lum = y[row * pitch + col]
            i = (row // 2) * pitch + (col & ~1)
            u = uv[i] - 128
            v = uv[i + 1] - 128
            r = lum + ((359 * v) >> 8)
            g = lum - ((88 * u + 183 * v) >> 8)
            b = lum + ((454 * u) >> 8)
            o = (row * width + col) * 3
            out[o] = 0 if r < 0 else 255 if r > 255 else r
            out[o + 1] = 0 if g < 0 else 255 if g > 255 else g
            out[o + 2] = 0 if b < 0 else 255 if b > 255 else b
    return bytes(out)


def write_ppm(path, width, height, rgb):
    path.write_bytes(b'P6\n%d %d\n255\n' % (width, height) + rgb)


def run_body(t, snapshot, base, extent, image, code, packet, report, args, restore_window,
             capture=True):
    """Upload, run Luma once, capture the frame, then restore and verify."""
    # 4: upload the bench image, verified chunk by chunk.
    t.blobs[base] = image
    t.upload(base, image)
    print(f'  uploaded + verified bench image {len(image):,} B @ 0x{base:08X}', flush=True)

    # 5: bounded execution of the real Luma entry.
    reply = hw.execute_verified(t, code, packet)
    (args.output / 'reply.bin').write_bytes(reply)
    parsed = hw.parse_reply(reply)
    report['reply'] = parsed
    out = parsed['output']
    if not parsed['magic_ok'] or out[0] != 1:
        raise dump.DumpError(f'bench payload refused or failed: out[0]={out[0]}')
    if out[1] != 0x4C554D42:
        raise dump.DumpError(f'Luma did not record its beacon; got 0x{out[1]:08X}')
    report['luma'] = dict(magic='LUMB', lcd_y=out[2], lcd_uv=out[3],
                          de_ctrl=out[4], de_video_y=out[5], de_video_uv=out[6],
                          panel=out[7], sensor_id=out[8], de_ctrl_saved=out[16])
    report['display_registers_restored'] = out[12] == 1
    print(f'  LUMA RAN: beacon=0x{out[1]:08X} lcd_y=0x{out[2]:08X} '
          f'de_video_y=0x{out[5]:08X} panel=0x{out[7]:08X}', flush=True)
    if not report['display_registers_restored']:
        raise dump.DumpError(f'payload could not restore the display registers '
                             f'(DE_CTRL after=0x{out[15]:08X} saved=0x{out[16]:08X})')

    # 6: read the frame Luma produced, straight out of RAM.
    y_addr, uv_addr = out[2], out[3]
    if not (base <= y_addr < base + extent) or not (base <= uv_addr < base + extent):
        raise dump.DumpError('Luma framebuffer addresses are outside the run window')
    luma_y = t.read_region(y_addr, 320 * 256)
    luma_uv = t.read_region(uv_addr, 160 * 256)
    (args.output / 'luma-frame.y.bin').write_bytes(luma_y)
    (args.output / 'luma-frame.uv.bin').write_bytes(luma_uv)
    write_ppm(args.output / 'luma-frame.ppm', 240, 320,
              yuv420_to_rgb(luma_y, luma_uv, 240, 320, 256))
    report['luma_frame_sha256'] = hashlib.sha256(luma_y).hexdigest()
    report['luma_frame_nonzero_bytes'] = sum(1 for b in luma_y if b)
    report['luma_frame_pitch'] = 256

    # 7: restore and verify, unless the window is an allocated block that is ours
    # to keep. Skipping is the point of allocated mode: there is no live firmware
    # state in the window, so there is nothing to put back.
    if restore_window is None:
        report['restore_exact'] = None
        report['restore_skipped'] = 'allocated block owned by this run'
        print('  restore skipped: window is an allocated block owned by this run', flush=True)
        return
    report['restore_exact'] = restore_window(t, snapshot)
    print(f'  window restored exactly: {report["restore_exact"]}', flush=True)
    if not report['restore_exact']:
        raise dump.DumpError('RAM window did not restore exactly')


def run_bounded(t, snapshot, base, extent, image, code, packet, report, args, restore_window):
    """Run Luma once and guarantee the window is restored, success or failure.

    The window sits inside the running firmware's allocator pool, so leaving it
    modified is what breaks the camera. Restoring on the failure path is not a
    nicety: the observed consequence of skipping it was the firmware hanging and
    needing a power cycle.
    """
    try:
        run_body(t, snapshot, base, extent, image, code, packet, report, args, restore_window)
    except Exception:
        if restore_window is None:
            report['restore_after_failure'] = None
            raise
        try:
            report['restore_after_failure'] = bool(restore_window(t, snapshot))
            print(f'  window restored after failure: {report["restore_after_failure"]}', flush=True)
        except Exception as inner:
            report['restore_error'] = f'{type(inner).__name__}: {inner}'
            print(f'  RESTORE FAILED: {report["restore_error"]}', flush=True)
        raise


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bench', type=Path, default=CORE / 'build/original/bench.json')
    ap.add_argument('--payload-dir', type=Path, default=HERE / 'payloads')
    ap.add_argument('--output', type=Path, required=True, help='new evidence directory')
    ap.add_argument('--survey-samples', type=int, default=8)
    ap.add_argument('--allocated', action='store_true',
                    help='the window is a block the firmware allocator has already handed out '
                         'and we keep. Skip snapshot/restore entirely: the block is exclusively '
                         'ours, the firmware holds no pointer into it, and nothing needs putting '
                         'back. This is the safe mode; the snapshot mode races the allocator.')
    ap.add_argument('--hold-frames', type=int, default=0,
                    help='wait for this many realised display commits before restoring, so '
                         'the panel actually shows Luma (0..64). DE commits are frame-gated, '
                         'so 0 captures the frame without ever displaying it. Implemented as '
                         'a bounded poll of DE_CTRL bit 1, not a sleep: a long sleep starves '
                         'the stock USB stack and wedges the device.')
    ap.add_argument('--restore-from', type=Path, default=None,
                    help='recovery mode: write this snapshot back into the window and verify, '
                         'then exit. Use after a failed run that could not restore itself.')
    a = ap.parse_args()

    bench = json.loads(a.bench.read_text())
    image_path = a.bench.parent / 'luma.bench.bin'
    image = image_path.read_bytes()
    base, extent = bench['base'], bench['extent']
    window = (base, extent)
    for key in ('entry', 'beacon', 'bss_start', 'bss_end'):
        if not base <= bench[key] < base + extent:
            raise SystemExit(f'bench.json {key} escapes the declared window')
    if len(image) > bench['bss_start'] - base + 1:
        raise SystemExit('image is larger than the text region bench.json describes')
    if hashlib.sha256(image).hexdigest() != bench['sha256']:
        raise SystemExit('bench image does not match bench.json; rebuild it')

    if a.restore_from is not None:
        snapshot = a.restore_from.read_bytes()
        if len(snapshot) != extent:
            raise SystemExit(f'--restore-from must be exactly {extent} bytes')
        a.output.mkdir(parents=True, exist_ok=False)
        report = dict(kind='window restore only', base=base, extent=extent,
                      source=str(a.restore_from), snapshot_sha256=hashlib.sha256(snapshot).hexdigest(),
                      started=datetime.datetime.now(datetime.timezone.utc).isoformat())

        def save():
            (a.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')

        def restore_window(t, data):
            t.upload(base, data)
            back = t.read_region(base, extent)
            report['restore_sha256'] = hashlib.sha256(back).hexdigest()
            report['restore_exact'] = back == data
            return report['restore_exact']

        try:
            usb, backend = dump.load_usb()
            cams, webcams = dump.find_cameras(usb, backend)
            dev = dump.select_camera(cams, webcams, 1)
            report['device'] = dump.device_label(dev)
            report['kind'] = 'window restore only'
            with RamTransport(usb, dev, 5000, window=window) as t:
                t.check_firmware()
                print(f'  restoring {extent:,} B @ 0x{base:08X} from {a.restore_from}', flush=True)
                report['restore_exact'] = restore_window(t, snapshot)
                print(f'  restore exact: {report["restore_exact"]}', flush=True)
            report['status'] = 'pass' if report['restore_exact'] else 'fail'
        except Exception as exc:
            report['status'] = 'fail'
            report['error'] = f'{type(exc).__name__}: {exc}'
            print(f'  FAILED: {report["error"]}', flush=True)
        finally:
            report['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
            save()
            print(f'  evidence: {a.output}', flush=True)
        return 0 if report['status'] == 'pass' else 1

    code = (a.payload_dir / 'lumarun.bin').read_bytes()
    if len(code) > hw.CODE_LIMIT:
        raise SystemExit('lumarun payload exceeds the code window')
    if not 0 <= a.hold_frames <= 64:
        raise SystemExit('--hold-frames must be 0..64')
    words = [bench['entry'], bench['beacon'], bench['bss_start'], bench['bss_end'],
             a.hold_frames]
    packet = hw.build_packet(words=words)

    a.output.mkdir(parents=True, exist_ok=False)
    (a.output / 'run-payload.bin').write_bytes(code)
    (a.output / 'run-input.bin').write_bytes(packet)
    (a.output / 'luma.bench.bin').write_bytes(image)
    report = dict(
        kind='hardware RAM run', base=base, extent=extent,
        image_bytes=len(image), image_sha256=bench['sha256'],
        entry=bench['entry'], beacon=bench['beacon'],
        bss=[bench['bss_start'], bench['bss_end']], hold_frames=a.hold_frames, input_words=words,
        started=datetime.datetime.now(datetime.timezone.utc).isoformat())

    def save():
        (a.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')

    def restore_window(t, snapshot):
        """Write the snapshot back and prove it landed. Used on both paths."""
        t.upload(base, snapshot)
        back = t.read_region(base, extent)
        report['restore_sha256'] = hashlib.sha256(back).hexdigest()
        report['restore_exact'] = back == snapshot
        return report['restore_exact']


    try:
        usb, backend = dump.load_usb()
        cams, webcams = dump.find_cameras(usb, backend)
        dev = dump.select_camera(cams, webcams, 1)
        report['device'] = dump.device_label(dev)
        with RamTransport(usb, dev, 5000, window=window) as t:
            t.check_firmware()
            print('  firmware signatures verified', flush=True)
            if t.hoist_active:
                raise dump.DumpError('a RAM hoist is active; power-cycle to stock before a '
                                     'RAM run so the window snapshot is meaningful')

            if a.allocated:
                # The block was already handed to us by the firmware allocator and we
                # keep it, so there is no live firmware state in the window and
                # nothing to snapshot or put back.
                report['mode'] = 'allocated block (no snapshot, no restore)'
                print('  allocated mode: no snapshot, no restore', flush=True)
                try:
                    run_bounded(t, None, base, extent, image, code, packet, report, a, None)
                except Exception:
                    raise
                report['status'] = 'pass'
            else:
                # 2/3: snapshot, then prove the window is quiet.
                t0 = time.monotonic()
                snapshot = t.read_region(base, extent)
                report['snapshot_seconds'] = round(time.monotonic() - t0, 1)
                report['snapshot_sha256'] = hashlib.sha256(snapshot).hexdigest()
                (a.output / 'window-snapshot.bin').write_bytes(snapshot)
                report['snapshot_nonzero_bytes'] = sum(1 for b in snapshot if b)
                step = max(1, extent // (a.survey_samples + 1))
                unstable = []
                for off in range(0, extent - CHUNK, step):
                    again = t.read_region(base + off, CHUNK)
                    if again != snapshot[off:off + CHUNK]:
                        unstable.append(off)
                report['unstable_samples'] = unstable
                print(f'  window {extent:,} B: {report["snapshot_nonzero_bytes"]:,} nonzero, '
                      f'{len(unstable)} unstable sample(s)', flush=True)
                if unstable:
                    raise dump.DumpError('RAM window is being written by the running firmware; '
                                         'refusing to run there')
                run_bounded(t, snapshot, base, extent, image, code, packet, report, a,
                            restore_window)
                report['status'] = 'pass'
    except Exception as exc:
        report['status'] = 'fail'
        report['error'] = f'{type(exc).__name__}: {exc}'
        print(f'  FAILED: {report["error"]}', flush=True)
    finally:
        report['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        save()
        print(f'  evidence: {a.output}', flush=True)
    return 0 if report['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
