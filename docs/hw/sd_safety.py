"""Refuse camera SD access if its USB block device is mounted by this host."""
from pathlib import Path


def require_unmounted(dev, sys_block=Path('/sys/class/block'),
                      mountinfo=Path('/proc/self/mountinfo')):
    ports = tuple(dev.port_numbers or ())
    if not ports:
        raise RuntimeError('cannot identify the camera USB port path; SD ownership is unverified')
    usb_node = f'{dev.bus}-' + '.'.join(map(str, ports))
    mounted = {}
    for line in mountinfo.read_text().splitlines():
        fields = line.split()
        if len(fields) >= 5:
            mounted[fields[2]] = fields[4]
    for block in sys_block.iterdir():
        if usb_node not in block.resolve().parts:
            continue
        number = (block / 'dev').read_text().strip()
        if number in mounted:
            raise RuntimeError(f'camera SD is mounted at {mounted[number]}; unmount it before continuing')
