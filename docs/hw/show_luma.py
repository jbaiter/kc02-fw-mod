#!/usr/bin/env python3
"""Show Luma running from RAM: allocate, build for that address, run, display.

One command, one USB claim, no snapshot to race:

  1. ask the stock allocator for a block and keep it (the block is then ours,
     so the firmware holds no pointer into it and nothing needs restoring)
  2. link the bounded Luma bench image for the address that allocation returned
  3. upload it, run Luma's real init + render + present, then wait for real
     display commits so the panel actually shows the frame
  4. read the frame Luma produced straight out of RAM and write a PNG

Step 2 has to happen after step 1 because the allocator chooses the address.
That is why this is one script and not two: the USB claim is held across the
build, so no other bench run can interleave.

Nothing touches flash. The block stays allocated afterwards, which is harmless
and keeps the address stable for the next run.
"""
import argparse
import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import execute_payload as hw
import dump_firmware as dump
import run_ram_image as rri

# Loaded by path: a top-level tools/build.py exists and would shadow this module
# if it were imported by name after execute_payload puts REPO/tools on sys.path.
_LUMA_BUILD = REPO / 'custom-firmware' / 'tools' / 'build.py'
_spec = importlib.util.spec_from_file_location('luma_build', _LUMA_BUILD)
luma_build = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(luma_build)

CORE = REPO / 'custom-firmware'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--payload-dir', type=Path, required=True,
                    help='directory containing ramalloc.bin and lumarun.bin')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--block-bytes', type=lambda v: int(v, 0), default=0x180000)
    ap.add_argument('--hold-frames', type=int, default=40)
    ap.add_argument('--exp', type=lambda v: int(v, 0), default=0,
                    help='exposure to program via the sensor fp_exp_gain_wr, 0 to skip')
    ap.add_argument('--gain', type=lambda v: int(v, 0), default=0x100)
    ap.add_argument('--test-pattern', action='store_true',
                    help='draw colour bars instead of the imager, to judge the display path alone')
    ap.add_argument('--takeover', action='store_true',
                    help='jump to Luma\'s real _start and never return. This is the only way '
                         'the panel can show Luma: the bench entry leaves interrupts enabled, '
                         'so the stock firmware keeps re-pointing DE and redrawing into '
                         'whatever buffer it finds, overwriting Luma every frame. _start sets '
                         'PICMR to zero, which stops the stock firmware touching the display. '
                         'The camera needs a pinhole reset afterwards; nothing is flashed, so '
                         'that is expected and reversible.')
    a = ap.parse_args()
    if not 0 <= a.hold_frames <= 64:
        raise SystemExit('--hold-frames must be 0..64')

    alloc_code = (a.payload_dir / 'ramalloc.bin').read_bytes()
    run_code = (a.payload_dir / 'lumarun.bin').read_bytes()
    alloc_packet = hw.build_packet(words=[a.block_bytes])

    a.output.mkdir(parents=True, exist_ok=False)
    (a.output / 'ramalloc.bin').write_bytes(alloc_code)
    (a.output / 'lumarun.bin').write_bytes(run_code)
    report = dict(kind='show Luma from RAM', hold_frames=a.hold_frames,
                  block_bytes=a.block_bytes,
                  started=datetime.datetime.now(datetime.timezone.utc).isoformat())

    def save():
        (a.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')

    try:
        prefix = luma_build.tool_prefix()
        usb, backend = dump.load_usb()
        cams, webcams = dump.find_cameras(usb, backend)
        dev = dump.select_camera(cams, webcams, 1)
        report['device'] = dump.device_label(dev)
        # Placeholder window: only needs to clear the harness regions. It is set
        # to the real allocated block as soon as the allocator names it.
        with rri.RamTransport(usb, dev, 5000, window=(0x02104000, 0x1000)) as t:
            t.check_firmware()
            print('  firmware signatures verified', flush=True)
            if t.hoist_active:
                raise dump.DumpError('a RAM hoist is active; power-cycle to stock first')

            # Release any block a previous run left allocated. The pool is only
            # ~6.98 MB and a 1.5 MB block per run exhausts it in about a dozen
            # runs, after which every allocation fails.
            free_code = (a.payload_dir / 'ramfree.bin').read_bytes()
            (a.output / 'ramfree.bin').write_bytes(free_code)
            for prior in (0x0235A200, 0x024DA200, 0x0215A200):
                try:
                    hw.execute_verified(t, free_code, hw.build_packet(words=[prior]))
                except Exception:
                    pass

            reply = hw.execute_verified(t, alloc_code, alloc_packet)
            parsed = hw.parse_reply(reply)
            base, size, ok = parsed['output'][0], parsed['output'][1], parsed['output'][2]
            report['alloc'] = dict(base=base, size=size, writable=ok,
                                   head=parsed['output'][3], tail=parsed['output'][4])
            if not parsed['magic_ok'] or not ok:
                raise dump.DumpError('allocator refused or the block is not writable')
            print(f'  allocator gave 0x{base:08X} ({size:,} bytes), writable', flush=True)

            # Scratch for the in-run CSI register snapshot: in the free tail of the
            # allocated block, above the bench image's .bss.
            scratch = base + 0x13C000
            record = luma_build.build_bench(CORE / 'build', prefix, base)
            image = (CORE / 'build/original/luma.bench.bin').read_bytes()
            extent = record['extent']
            if base + extent > base + size:
                raise dump.DumpError('bench image does not fit the allocated block')
            report['bench'] = record
            report['luma_image_sha256'] = record['sha256']
            t.window = (base, extent)

            words = [record['entry'], record['beacon'], record['bss_start'],
                     record['bss_end'], a.hold_frames, a.exp, a.gain,
                     1 if a.test_pattern else 0,
                     record['sensor_y'], record['sensor_uv'], scratch]
            packet = hw.build_packet(words=words)
            (a.output / 'run-input.bin').write_bytes(packet)
            t.blobs[base] = image
            t.upload(base, image)
            print(f'  uploaded + verified {len(image):,} B @ 0x{base:08X}', flush=True)

            if a.takeover:
                # Jump to Luma's own entry. It disables interrupts, zeroes its own
                # bss, sets its own stack and runs its main loop forever, so this
                # call never returns: the stock firmware stops owning the display
                # and the panel shows Luma. The resulting USB timeout is the
                # expected outcome, not a failure, so it must not be retried.
                words = [record['start'], record['beacon'], record['bss_start'],
                         record['bss_end'], 0, a.exp, a.gain,
                         1 if a.test_pattern else 0,
                         record['sensor_y'], record['sensor_uv'], scratch]
                packet = hw.build_packet(words=words)
                (a.output / 'takeover-input.bin').write_bytes(packet)
                print('  >>> jumping to Luma _start; the panel should now show Luma <<<', flush=True)
                try:
                    hw.execute_verified(t, run_code, packet)
                    report['takeover'] = 'returned unexpectedly'
                except Exception as exc:
                    report['takeover'] = f'no return, as expected ({type(exc).__name__})'
                report['status'] = 'takeover'
                report['note'] = ('Luma is running and owns the display. Use the pinhole reset '
                                  'to return to the stock firmware; no flash was written.')
                print(f'  {report["takeover"]}', flush=True)
                print('  camera now needs a pinhole reset to return to stock', flush=True)
                return 0

            reply = hw.execute_verified(t, run_code, packet)
            (a.output / 'reply.bin').write_bytes(reply)
            parsed = hw.parse_reply(reply)
            report['reply'] = parsed
            out = parsed['output']
            if not parsed['magic_ok'] or out[0] != 1 or out[1] != 0x4C554D42:
                raise dump.DumpError(f'Luma did not complete: out[0]={out[0]} out[1]=0x{out[1]:08X}')
            guard_polls = {'frames_completed': out[26], 'max_guard': out[27]}
            report['luma'] = dict(magic='LUMB', lcd_y=out[2], lcd_uv=out[3],
                                  de_video_y=out[5], panel=out[7], sensor_id=out[8],
                                  display_registers_restored=out[12] == 1,
                                  commit_guard_polls=guard_polls)
            report['luma']['clock_ms_across_spin'] = out[25]
            ladder = out[24]
            report['luma']['keypad_ladder'] = dict(low=ladder >> 16, high=ladder & 0xffff)
            # All of these are measured INSIDE the payload, before it returns and
            # before the stock firmware resumes ownership.
            report['luma']['sensor_bringup'] = dict(
                id_ack=out[9], id=out[10], table_written=out[11], raw_ack=out[17],
                dbg_reg=out[22], dbg_read=out[23])
            report['luma']['csi_in_run'] = dict(b000=out[18], b018=out[19],
                                                b00c=out[20], b014=out[21])
            report['luma']['capture'] = dict(y_data=out[28], y_zero=out[29],
                                             uv_data=out[30], uv_zero=out[31])
            print(f'  keypad ladder: low={ladder >> 16} high={ladder & 0xffff} '
                  f'(idle is ~1023; a held button pulls it to a ladder level)', flush=True)
            print(f'  LUMA RAN: de_video_y=0x{out[5]:08X} panel=0x{out[7]:08X} '
                  f'regs_restored={out[12] == 1} clock_advanced_ms={out[25]}', flush=True)
            print(f'  IN-RUN sensor: ack={out[9]} id=0x{out[10]:02X} table={out[11]} '
                  f'raw_ack={out[17]} dbg_reg={out[22]} dbg_read={out[23]}', flush=True)
            print(f'  IN-RUN CSI: b000=0x{out[18]:08X} b018=0x{out[19]:08X} '
                  f'b00c=0x{out[20]:08X} b014=0x{out[21]:08X}', flush=True)
            print(f'  IN-RUN capture: Y data={out[28]}/57600 zero={out[29]} | '
                  f'UV data={out[30]}/28800 zero={out[31]}', flush=True)
            report['luma']['csi_service'] = dict(seen=out[32], polls=out[33], rearms=out[34])
            print(f'  IN-RUN CSI service: events_seen=0x{out[32]:08X} polls={out[33]} '
                  f'rearms={out[34]}', flush=True)
            mclk_hz = 288000000 // max(1, ((out[39] >> 8) & 0x1f) + 1)
            report['luma']['mclk'] = dict(ops_mclk_field=out[38], reg_884c=out[39],
                                          divider=(out[39] >> 8) & 0x1f,
                                          implied_hz=mclk_hz)
            print(f'  IN-RUN MCLK: ops->mclk={out[38]} (0x{out[38]:X})  0x884c=0x{out[39]:08X} '
                  f'divider={(out[39] >> 8) & 0x1f} -> ~{mclk_hz/1e6:.1f} MHz', flush=True)
            ident = {0x020e4ba0: 'H63P (real)', 0x02083cb0: 'FALLBACK test pattern'}.get(
                out[35], f'0x{out[35]:08X}')
            report['luma']['vendor_autodetect'] = dict(ident=out[35], adapter=out[36],
                                                       active=out[37], ident_name=ident)
            print(f'  IN-RUN vendor autodetect: ident={ident} adapter=0x{out[36]:08X} '
                  f'active=0x{out[37]:08X}', flush=True)
            if out[25] == 0:
                print('  WARNING: the millisecond clock did not advance; the render gate '
                      'can only ever draw one frame', flush=True)
            if a.hold_frames:
                print(f'  waited for {a.hold_frames} commit(s); {guard_polls}', flush=True)

            # Hand the block back so the next run starts from a clean pool.
            block = report['alloc']['base']
            # POST-RUN read, kept only as a cross-check. A bounded entry leaves
            # interrupts enabled, so by the time this runs the stock firmware has
            # resumed and re-owns these blocks -- this is the STOCK's state, not
            # Luma's. The in-run digest printed above is the measurement that
            # counts.
            # Sample twice. The LCD transfer writes Y and UV as separate
            # transfers, so a read that lands between them shows Y populated and
            # UV not -- which is what the "intermittent chroma" symptom looks
            # like. Keep whichever sample has the more populated chroma plane.
            sy, suv = record['sensor_y'], record['sensor_uv']
            best = None
            for sample in range(3):
                cy = t.read_region(sy, 320 * 180)
                cuv = t.read_region(suv, 320 * 90)
                filled = sum(1 for b in cuv if b != 0x80)
                print(f'    sample {sample+1}: chroma bytes differing from 128 = {filled}', flush=True)
                if best is None or filled > best[0]:
                    best = (filled, cy, cuv)
            _, raw_y, raw_uv = best
            (a.output / 'csi-raw.y.bin').write_bytes(raw_y)
            (a.output / 'csi-raw.uv.bin').write_bytes(raw_uv)
            if scratch:
                regs = t.read_region(scratch, 512)
                (a.output / 'csi-regs.bin').write_bytes(regs)
                import struct as _s
                vals = _s.unpack('<128I', regs)
                nz = [(0xb000 + 4 * k, v) for k, v in enumerate(vals) if v]
                print(f'  IN-RUN CSI block snapshot: {len(nz)}/128 registers non-zero',
                      flush=True)
                for addr, v in nz:
                    print(f'     0x{addr:04x} = 0x{v:08X}', flush=True)
            import collections as _c, statistics as _st
            print(f'  CSI raw Y: mean {_st.mean(raw_y):.1f} stdev {_st.pstdev(raw_y):.1f} '
                  f'distinct {len(_c.Counter(raw_y))} top {_c.Counter(raw_y).most_common(4)}',
                  flush=True)
            print(f'  CSI raw UV: mean {_st.mean(raw_uv):.1f} stdev {_st.pstdev(raw_uv):.1f} '
                  f'top {_c.Counter(raw_uv).most_common(4)}', flush=True)
            from PIL import Image as _I
            _I.frombytes('L', (320, 180), raw_y).save(a.output / 'csi-raw.png')

            y = t.read_region(out[2], 320 * 256)
            uv = t.read_region(out[3], 160 * 256)
            (a.output / 'luma-frame.y.bin').write_bytes(y)
            (a.output / 'luma-frame.uv.bin').write_bytes(uv)
            rri.write_ppm(a.output / 'luma-frame.ppm', 240, 320,
                          rri.yuv420_to_rgb(y, uv, 240, 320, 256))
            report['luma_frame_sha256'] = hashlib.sha256(y).hexdigest()
            report['luma_frame_nonzero_bytes'] = sum(1 for b in y if b)
            report['luma_frame_pitch'] = 256
            print(f'  frame captured: {report["luma_frame_nonzero_bytes"]:,}/{240*320:,} nonzero',
                  flush=True)
            print(f'  SENSOR id: ack={out[9]} id=0x{out[10]:02x} raw_ack={out[17]} '
                  f'rx=0x{out[18]:02x}', flush=True)
            print(f'  CSI after bring-up: b000=0x{out[18]:08x} b018=0x{out[19]:08x} '
                  f'b00c=0x{out[20]:08x} b014=0x{out[21]:08x}', flush=True)
            if not (out[18] & 1):
                print('  CSI is NOT enabled (b000 bit0 clear) - capture was never armed',
                      flush=True)
            elif not out[19]:
                print('  CSI enabled but b018 shows no transfer state', flush=True)
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
