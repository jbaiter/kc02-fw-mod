import sys
from pathlib import Path
REPO = Path('/home/jbaiter/.src/kc02-fw-mod')
sys.path.insert(0, str(REPO / 'tools'))
import dump_firmware as dump
usb, backend = dump.load_usb()
print("backend:", backend)
cams, webcams = dump.find_cameras(usb, backend)
print(f"cameras={[dump.device_label(c) for c in cams]}  webcams={[dump.device_label(w) for w in webcams]}")
dev = dump.select_camera(cams, webcams, 1)
print("device:", dump.device_label(dev), "bus", dev.bus, "addr", dev.address)
cfg = dev.get_active_configuration()
print(f"active config {cfg.bConfigurationValue}, interfaces: {[(i.bInterfaceNumber, i.bInterfaceClass, i.bInterfaceSubClass) for i in cfg]}")
print("INTERFACE constant in dumper:", dump.INTERFACE)
for i in cfg:
    n = i.bInterfaceNumber
    try:
        active = dev.is_kernel_driver_active(n)
    except Exception as e:
        active = f"<{type(e).__name__}: {e}>"
    print(f"  iface {n}: kernel_driver_active={active}")
    if active is True:
        try:
            dev.detach_kernel_driver(n)
            print(f"    detach iface {n}: OK")
        except Exception as e:
            print(f"    detach iface {n}: FAILED {type(e).__name__}: {e}")
try:
    usb.util.claim_interface(dev, dump.INTERFACE)
    print("claim_interface: OK")
    usb.util.release_interface(dev, dump.INTERFACE)
except Exception as e:
    print(f"claim_interface: FAILED {type(e).__name__}: {e}")
print("sysfs unbind writable by us:", Path('/sys/bus/usb/drivers/usb-storage/unbind').exists(),
      "writable=", __import__('os').access('/sys/bus/usb/drivers/usb-storage/unbind', 2))
