"""Offline guards for the live RAM-run harness; no camera access."""
import importlib.util
from pathlib import Path
from unittest.mock import patch

import pytest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('show_luma_test', ROOT / 'docs/hw/show_luma.py')
show = importlib.util.module_from_spec(spec)
spec.loader.exec_module(show)


def record(bss_offset=0x168f04):
    base = 0x0235a200
    return dict(base=base, extent=0x170000, bss_end=base+bss_offset,
                stack_top=base+0x170000-16)


def test_scratch_follows_actual_bss_not_old_fixed_offset():
    r = record()
    address = show.capture_scratch(r)
    assert address >= r['bss_end'] > r['base'] + 0x168000
    assert address % 32 == 0
    assert address + 512 <= r['stack_top'] - 16384


@pytest.mark.parametrize('bss_offset', [0x16c000, 0x180000, -0x1000])
def test_scratch_refuses_overlap_or_outside_window(bss_offset):
    with pytest.raises(show.dump.DumpError, match='no safe CSI snapshot scratch'):
        show.capture_scratch(record(bss_offset))


def test_scratch_rejects_nonpositive_size():
    with pytest.raises(show.dump.DumpError):
        show.capture_scratch(record(), size=0)


def test_stock_signature_reads_do_not_authorise_writes():
    t = object.__new__(show.LumaRamTransport)
    t.window = (0x0235a200, 0x2c0000)
    t.blobs = {}
    for address, signature, _ in show.LUMA_STOCK_SITES:
        assert t._approved_read(address, len(signature)) == address
        assert t._approved_write(address, signature) is None
        assert t._approved_read(address, len(signature) - 1) is None


def test_stock_signature_mismatch_closes_execution_gate():
    t = object.__new__(show.LumaRamTransport)
    t.firmware_ok = True
    t.read_region = lambda address, size: b'\x00' * size
    with patch.object(show.rri.RamTransport, 'check_firmware', return_value=None):
        with pytest.raises(show.dump.DumpError, match='signature differs'):
            t.check_firmware()
    assert not t.firmware_ok
