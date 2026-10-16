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

# Canonical original.bin bodies, independently read from the Ghidra image.
# Read-only approvals here do not authorise uploads to any of these addresses.
LUMA_STOCK_SITES = (
    (0x0203e5c4, bytes.fromhex(
        'fc4fe1d7f40fe1d7f817e1d7f0ff219c001801d496ffff0700008ba800006184000004bc26000010c0ff409c3f00639c'), 'allocator'),
    (0x020583f0, bytes.fromhex(
        '0e0240180000609c203242a8fc4fe1d7ec0fe1d7f477e1d7f897e1d7001802d80d024018ecff219c811e42a80100609c'), 'sensor init'),
    (0x02058b78, bytes.fromhex(
        'fc4fe1d7f417e1d7f877e1d7f00fe1d7000044a8d0ff219c080280180400a09c804984a80000c3a90400619cc37d0004'), 'H63P exposure writer'),
    (0x020349f8, bytes.fromhex(
        'e417e1d7ec97e1d7f0a7e1d7f4b7e1d7f8c7e1d7fc4fe1d7e00fe1d7e877e1d7e0ff219cffffe7a420008194ffffc5a6'), 'CSI LCD scaler'),
)


class LumaRamTransport(rri.RamTransport):
    def _approved_read(self, buffer, length):
        if any((buffer, length) == (a, len(b)) for a, b, _ in LUMA_STOCK_SITES):
            return buffer
        return super()._approved_read(buffer, length)

    def check_firmware(self):
        super().check_firmware()
        for address, expected, name in LUMA_STOCK_SITES:
            if self.read_region(address, len(expected)) != expected:
                self.firmware_ok = False
                raise dump.DumpError(f'Unsupported firmware: {name} signature differs')


def capture_scratch(record, size=512):
    """Reserve aligned scratch above BSS, below the 16 KiB stack reserve."""
    base, extent = record['base'], record['extent']
    address = (record['bss_end'] + 31) & ~31
    limit = min(base + extent, record['stack_top'] - 16384)
    if size <= 0 or not base <= address or address + size > limit:
        raise dump.DumpError('no safe CSI snapshot scratch between BSS and stack')
    return address


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--payload-dir', type=Path, required=True,
                    help='directory containing ramalloc.bin and lumarun.bin')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--block-bytes', type=lambda v: int(v, 0), default=0x300000)
    ap.add_argument('--hold-frames', type=int, default=40)
    ap.add_argument('--exp', type=lambda v: int(v, 0), default=0,
                    help='exposure to program via the sensor fp_exp_gain_wr, 0 to skip')
    ap.add_argument('--gain', type=lambda v: int(v, 0), default=0x100)
    ap.add_argument('--test-pattern', action='store_true',
                    help='draw colour bars instead of the imager, to judge the display path alone')
    ap.add_argument('--size', type=lambda v: int(v, 0), default=0,
                    help='override CSI_IMAGE_SIZE (b01c) after the bring-up, as '
                         '(h-1)<<16|(w-1). 0x02CF04FF is 1280x720.')
    ap.add_argument('--overlay', action='store_true',
                    help='draw the capture telemetry on the panel (packet word 11). '
                         'Needed because a takeover run never returns, so the panel is '
                         'the only channel that reports the steady state.')
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
        with LumaRamTransport(usb, dev, 5000, window=(0x02104000, 0x1000)) as t:
            t.check_firmware()
            print('  firmware signatures verified', flush=True)
            if t.hoist_active:
                raise dump.DumpError('a RAM hoist is active; power-cycle to stock first')

            # Do not free guessed addresses from older runs: after a reset those
            # addresses may belong to stock firmware, and CSI may still target a
            # prior allocation. Keep this run's allocation until a pinhole reset.
            # If the pool is exhausted, reset rather than reclaim unknown owners.

            reply = hw.execute_verified(t, alloc_code, alloc_packet)
            parsed = hw.parse_reply(reply)
            base, size, ok = parsed['output'][0], parsed['output'][1], parsed['output'][2]
            report['alloc'] = dict(base=base, size=size, writable=ok,
                                   head=parsed['output'][3], tail=parsed['output'][4])
            if not parsed['magic_ok'] or not ok:
                raise dump.DumpError('allocator refused or the block is not writable')
            print(f'  allocator gave 0x{base:08X} ({size:,} bytes), writable', flush=True)

            record = luma_build.build_bench(CORE / 'build', prefix, base)
            image = (CORE / 'build/original/luma.bench.bin').read_bytes()
            extent = record['extent']
            # Display double buffering grew BSS past the old fixed 0x168000
            # scratch offset. Derive it from this build and guard the stack.
            scratch = capture_scratch(record)
            report['capture_scratch'] = scratch
            if base + extent > base + size:
                raise dump.DumpError('bench image does not fit the allocated block')
            report['bench'] = record
            report['luma_image_sha256'] = record['sha256']
            t.window = (base, extent)

            words = [record['entry'], record['beacon'], record['bss_start'],
                     record['bss_end'], a.hold_frames, a.exp, a.gain,
                     1 if a.test_pattern else 0,
                     record['sensor_y'], record['sensor_uv'], scratch,
                     1 if a.overlay else 0, 0, a.size]
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
                         record['sensor_y'], record['sensor_uv'], scratch,
                         1 if a.overlay else 0, 0, a.size]
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
            de_timeouts = int.from_bytes(t.read_region(record['beacon'] + 34 * 4, 4), 'little')
            capture_fault = int.from_bytes(t.read_region(record['beacon'] + 35 * 4, 4), 'little')
            report['luma']['de_present_timeouts'] = de_timeouts
            report['luma']['capture_fault'] = capture_fault
            # Preserve configuration even on a refused panel trial.
            regs = t.read_region(scratch, 512)
            (a.output / 'csi-regs.bin').write_bytes(regs)
            values = [int.from_bytes(regs[k:k+4], 'little') for k in range(0, 512, 4)]
            report['luma']['scaler'] = {
                hex(addr): values[(addr - 0xb000) // 4]
                for addr in (0xb004, 0xb008, 0xb068, 0xb180, 0xb184, 0xb188)}
            print('  scaler:', {k: hex(v) for k, v in report['luma']['scaler'].items()}, flush=True)
            print(f'  DE buffer-ownership timeouts: {de_timeouts}; '
                  f'CSI bounds/setup fault: 0x{capture_fault:x}', flush=True)
            if capture_fault:
                for key, size, filename in (
                        ('capture_y_dma', 320 * 180, 'y-guard-spill.bin'),
                        ('capture_uv_dma', 320 * 90, 'uv-guard-spill.bin')):
                    (a.output / filename).write_bytes(t.read_region(record[key] + size, 256))
                raise dump.DumpError(f'CSI guard/setup failure 0x{capture_fault:x}; no panel trial')
            if de_timeouts:
                raise dump.DumpError('presenter could not confirm its submitted buffer became active')
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
            print(f'  IN-RUN buffers: readable set Y data={out[28]}/57600, '
                  f'OTHER set Y data={out[38]}/57600 '
                  f'{"(both written - swap OK)" if out[38] > 50000 else "(ONE SET STALE - swap not taking effect)"}',
                  flush=True)
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

            # These symbols now name separate CPU-owned snapshots, not members
            # of an adjacent DMA/snapshot array. Refuse an older image layout.
            if record.get('capture_layout') != 'guarded-dma-separate-cpu-snapshot-v1':
                raise dump.DumpError('unsupported capture layout')
            sy, suv = record['sensor_y'], record['sensor_uv']
            raw_y = t.read_region(sy, 320 * 180)
            raw_uv = t.read_region(suv, 320 * 90)
            stable = (t.read_region(sy, 320 * 180) == raw_y and
                      t.read_region(suv, 320 * 90) == raw_uv)
            report['luma']['capture_readback'] = dict(
                source='separate CPU snapshot', y=sy, uv=suv, stable=stable)
            if not stable:
                raise dump.DumpError('CPU snapshot changed during readback; capture is untrustworthy')
            (a.output / 'csi-raw.y.bin').write_bytes(raw_y)
            (a.output / 'csi-raw.uv.bin').write_bytes(raw_uv)
            if scratch:
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
