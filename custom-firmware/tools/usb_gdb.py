#!/usr/bin/env python3
"""Experimental localhost GDB <-> Luma vendor-bulk bridge. No stock USB access.

Requires the new bring-up USB firmware, not QEMU's GDB stub or stock firmware.
No kernel-driver detach, USB reset, mass storage, memory commands or flashing.
The bridge forwards RSP bytes; firmware retains the memory/access policy.
"""
import argparse
import select
import socket
import sys

STOCK = {(0x0219, 0x3280), (0x1908, 0x3282), (0x1908, 0x3283)}


def check_identity(vid, pid):
    if not 1 <= vid <= 0xffff or not 1 <= pid <= 0xffff or (vid, pid) in STOCK:
        raise ValueError('use an explicit distinct development VID/PID, not a stock camera')


def check_interface(config):
    choices = [i for i in config if (i.bInterfaceClass, i.bInterfaceSubClass, i.bInterfaceProtocol,
                                    i.bAlternateSetting) == (0xff, 0x4c, 1, 0)]
    if len(choices) != 1:
        raise ValueError('expected exactly one Luma debug interface ff/4c/01')
    interface = choices[0]
    endpoints = {e.bEndpointAddress: e for e in interface}
    if (set(endpoints) != {0x01, 0x81} or len(list(interface)) != 2 or
            any((e.bmAttributes & 3) != 2 or e.wMaxPacketSize != 64 for e in endpoints.values())):
        raise ValueError('unexpected debug bulk endpoints; refusing to claim')
    return interface.bInterfaceNumber


def write_all(device, data, timeout):
    # Treat any partial or unknown USB OUT outcome as a failure. Do not retry it.
    if device.write(0x01, data, timeout=timeout) != len(data):
        raise OSError('partial USB OUT; stop and inspect rather than retrying')


def forward(client, device, usb, timeout=100):
    client.settimeout(2)
    while True:
        ready, _, _ = select.select([client], [], [], .005)
        if ready:
            data = client.recv(64)
            if not data:
                return
            write_all(device, data, timeout)
        try:
            data = bytes(device.read(0x81, 64, timeout=20))
        except usb.core.USBTimeoutError:
            continue
        if data:
            client.sendall(data)


def revoke(device, interface):
    """Revoke only, never authorize or resume, and only through the debug
    function: the message is addressed to that interface, so it cannot reach the
    mass-storage function. A failure leaves the session status UNKNOWN."""
    device.ctrl_transfer(0x40, 0x5f, 0, interface, b'', timeout=1000)


def serve(device, usb, interface, port):
    claimed = False
    try:
        # The storage function carries class 08 and belongs to the OS disk driver;
        # only the vendor debug function is claimed here.
        if device.is_kernel_driver_active(interface):
            raise ValueError('debug interface has a kernel driver; refusing to detach it')
        usb.util.claim_interface(device, interface)
        claimed = True
        revoke(device, interface)
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(('127.0.0.1', port))
            listener.listen(1)
            print(f'Listening on 127.0.0.1:{listener.getsockname()[1]}', flush=True)
            print('On the camera: release OK, press OK, then release OK. Then attach GDB.', flush=True)
            client, _ = listener.accept()
            with client:
                forward(client, device, usb)
    finally:
        if claimed:
            try:
                revoke(device, interface)
            except Exception as exc:
                print(f'WARNING: debug revocation unconfirmed ({exc}); unplug/power-cycle.', file=sys.stderr)
            usb.util.release_interface(device, interface)
        usb.util.dispose_resources(device)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--vid', type=lambda s: int(s, 0), required=True)
    p.add_argument('--pid', type=lambda s: int(s, 0), required=True)
    p.add_argument('--port', type=int, default=3333)
    a = p.parse_args()
    try:
        check_identity(a.vid, a.pid)
        if not 1 <= a.port <= 65535:
            raise ValueError('invalid localhost TCP port')
        import usb.core
        import usb.util
        devices = list(usb.core.find(find_all=True, idVendor=a.vid, idProduct=a.pid))
        if len(devices) != 1:
            raise ValueError(f'expected exactly one matching device, found {len(devices)}')
        device = devices[0]
        # Do not set configuration: that would reset/revoke an existing session.
        interface = check_interface(device.get_active_configuration())
        serve(device, usb, interface, a.port)
    except (ValueError, OSError, ImportError) as exc:
        p.error(str(exc))
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
