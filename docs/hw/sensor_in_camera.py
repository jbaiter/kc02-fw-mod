#!/usr/bin/env python3
"""Read the sensor identity while the camera is in CAMERA mode.

Every earlier IIC attempt ran in USB mode, where the stock firmware has not run
sensorInit. Stock powers the imager and releases its reset on camera entry, so in
USB mode the sensor may simply not be listening -- which would explain why both
IIC buses failed for our driver AND for stock's own helper.

Getting there has a wrinkle worth recording. Camera entry keeps the USB mass
storage interface enumerated, but it tears down the in-flight BOT transaction, so
the handle that asked for the mode change comes back with a timeout and the
camera's BOT state machine is left wedged. It does not recover on its own, which
is why an earlier version of this tool could never get a command in afterwards.
The fix is to reset the USB port *while we still hold the interface claim* (the
kernel driver is detached then, so the reset is not refused as busy), then
rediscover the device and verify firmware and mode before probing.

No flash access of any kind.
"""
import argparse
import datetime
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

MODE_WORD = 0x020CCD54


class CameraTransport(hw.PayloadTransport):
    def _approved_read(self, buffer, length):
        if 0x020CCD44 <= buffer and buffer + length <= 0x020CCD44 + 24 and 0 < length <= hw.SIZE:
            return buffer
        return super()._approved_read(buffer, length)


def find_camera(deadline):
    last = None
    while time.monotonic() < deadline:
        try:
            usb, backend = dump.load_usb()
            cams, _ = dump.find_cameras(usb, backend)
            if cams:
                return usb, backend, cams[0]
            last = 'no camera on the bus'
        except Exception as exc:
            last = f'{type(exc).__name__}: {exc}'
        time.sleep(0.25)
    raise dump.DumpError(f'camera never appeared ({last})')


def read_mode(t):
    return struct.unpack('<6I', t.read_region(0x020CCD44, 24))[4]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--payload-dir', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--probe-seconds', type=float, default=20.0)
    a = ap.parse_args()

    mode_code = (a.payload_dir / 'mode.bin').read_bytes()
    probe_code = (a.payload_dir / 'sensoridprobe.bin').read_bytes()
    mode_packet = hw.build_packet(words=[3, 0])   # 3 = camera, never power-off
    sensor_packet = hw.build_packet(words=[1])    # 1 = apply the power sequence

    a.output.mkdir(parents=True, exist_ok=False)
    (a.output / 'mode.bin').write_bytes(mode_code)
    (a.output / 'sensoridprobe.bin').write_bytes(probe_code)
    report = dict(kind='sensor identity in camera mode',
                  started=datetime.datetime.now(datetime.timezone.utc).isoformat())

    def save():
        (a.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')

    try:
        usb, backend, dev = find_camera(time.monotonic() + a.probe_seconds)
        report['device_before'] = dump.device_label(dev)
        print(f'  camera: {report["device_before"]}', flush=True)

        with CameraTransport(usb, dev, 5000) as t:
            t.check_firmware()
            print('  firmware signatures verified', flush=True)
            if t.hoist_active:
                raise dump.DumpError('a RAM hoist is active; power-cycle to stock first')
            before = read_mode(t)
            report['mode_before'] = before
            print(f'  mode before: {before}', flush=True)
            if before not in (3, 10):
                raise dump.DumpError(f'unexpected mode {before}')

            if before == 10:
                print('  requesting camera mode (3) ...', flush=True)
                try:
                    reply = hw.execute_verified(t, mode_code, mode_packet)
                    (a.output / 'mode-reply.bin').write_bytes(reply)
                    report['mode_request'] = 'returned'
                except Exception as exc:
                    # Expected: camera entry tears down this transaction.
                    report['mode_request'] = f'departing handle: {type(exc).__name__}'
                    print(f'  handle went away ({type(exc).__name__}), as expected', flush=True)

                # Reset the port now, while the kernel driver is still detached,
                # so the camera's BOT state machine is not left mid-transaction.
                try:
                    t.dev.reset()
                    report['usb_reset'] = 'ok'
                    print('  USB port reset issued', flush=True)
                except Exception as exc:
                    report['usb_reset'] = f'{type(exc).__name__}: {exc}'
                    print(f'  USB reset failed: {report["usb_reset"]}', flush=True)
                try:
                    t.close()
                except Exception:
                    pass
            else:
                print('  already in camera mode', flush=True)

        # Rediscover and verify before trusting anything.
        usb2, backend2, dev2 = find_camera(time.monotonic() + max(a.probe_seconds, 15.0))
        report['device_after'] = dump.device_label(dev2)
        print(f'  camera now: {report["device_after"]}', flush=True)

        last_error = None
        for attempt in range(6):
            try:
                with CameraTransport(usb2, dev2, 5000) as t2:
                    t2.check_firmware()
                    mode = read_mode(t2)
                    report['mode_after'] = mode
                    print(f'  mode now: {mode} (attempt {attempt + 1})', flush=True)
                    if mode != 3:
                        raise dump.DumpError(f'camera mode did not arrive (mode={mode})')
                    time.sleep(0.5)
                    print('  reading sensor identity in camera mode ...', flush=True)
                    reply = hw.execute_verified(t2, probe_code, sensor_packet)
                    (a.output / 'sensor-reply.bin').write_bytes(reply)
                    parsed = hw.parse_reply(reply)
                    out = parsed['output']
                    report['sensor_probe'] = dict(
                        ldo_after=out[11], mclk_div_after=out[12],
                        id_status_16m=out[13], id_value_16m=out[14],
                        id_status_24m=out[15], id_value_24m=out[16],
                        reg00_status=out[17], reg00=out[18],
                        regf0_status=out[19], regf0=out[20],
                        restored=out[24])
                    print(f'  id @16MHz: status={out[13]} value=0x{out[14]:02x}', flush=True)
                    print(f'  id @24MHz: status={out[15]} value=0x{out[16]:02x}', flush=True)
                    print(f'  reg0x00: status={out[17]} value=0x{out[18]:02x}', flush=True)
                    if out[13] == 1 and out[14] == 0x48:
                        print('  *** H63P ANSWERED 0x48: the imager is reachable ***', flush=True)
                    elif out[13] == 1:
                        print(f'  bus answered, id byte 0x{out[14]:02x} (expected 0x48)', flush=True)
                    else:
                        print('  no answer, even in camera mode', flush=True)
                report['status'] = 'pass'
                last_error = None
                break
            except Exception as exc:
                last_error = exc
                print(f'  attempt {attempt + 1} failed: {type(exc).__name__}: {exc}', flush=True)
                time.sleep(1.5)
                try:
                    usb2, backend2, dev2 = find_camera(time.monotonic() + 5.0)
                except Exception:
                    pass
        if last_error is not None:
            raise last_error
    except Exception as exc:
        report['status'] = 'fail'
        report['error'] = f'{type(exc).__name__}: {exc}'
        print(f'  FAILED: {report["error"]}', flush=True)
    finally:
        report['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        save()
        print(f'  evidence: {a.output}', flush=True)
        print('  camera left in camera mode; pinhole reset returns it to stock', flush=True)
    return 0 if report.get('status') == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
