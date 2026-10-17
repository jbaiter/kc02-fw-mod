"""Host mount ownership checks; no USB device or real block device access."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sd_safety_test', ROOT / 'docs/hw/sd_safety.py')
safety = importlib.util.module_from_spec(spec)
spec.loader.exec_module(safety)


def fixture(tmp_path, usb_node='3-2.4', mounted=True):
    blocks = tmp_path / 'class-block'
    blocks.mkdir()
    target = tmp_path / 'devices' / usb_node / 'host0' / 'block' / 'sdb' / 'sdb1'
    target.mkdir(parents=True)
    (target / 'dev').write_text('8:17\n')
    (blocks / 'sdb1').symlink_to(target)
    info = tmp_path / 'mountinfo'
    info.write_text('38 26 8:17 / /media/CAMERA rw - vfat /dev/sdb1 rw\n' if mounted else '')
    return blocks, info


def test_refuses_mounted_camera_partition(tmp_path):
    blocks, info = fixture(tmp_path)
    dev = SimpleNamespace(bus=3, port_numbers=(2, 4))
    with pytest.raises(RuntimeError, match='camera SD is mounted'):
        safety.require_unmounted(dev, blocks, info)


def test_unmounted_camera_is_allowed(tmp_path):
    blocks, info = fixture(tmp_path, mounted=False)
    safety.require_unmounted(SimpleNamespace(bus=3, port_numbers=(2, 4)), blocks, info)


def test_unrelated_usb_storage_is_not_touched(tmp_path):
    blocks, info = fixture(tmp_path, usb_node='3-2.5')
    safety.require_unmounted(SimpleNamespace(bus=3, port_numbers=(2, 4)), blocks, info)


def test_unknown_camera_port_fails_closed(tmp_path):
    with pytest.raises(RuntimeError, match='ownership is unverified'):
        safety.require_unmounted(SimpleNamespace(bus=3, port_numbers=None))
