"""Offline checks for the hardware RAM-run tool; no camera is accessed.

These guard the safety properties the RAM run depends on: the target window may
never overlap the harness's own regions, a run refuses to start without a
trustworthy snapshot, and a failed run still puts the window back. The hardware
behaviour itself is verified separately on the device.
"""
import importlib.util
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

spec = importlib.util.spec_from_file_location('hw_run_ram_image', ROOT / 'docs/hw/run_ram_image.py')
ram = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ram)
hw = ram.hw
dump = ram.dump

BENCH = ROOT / 'custom-firmware/build/original/bench.json'


class WindowGuardTests(unittest.TestCase):
    def test_window_must_be_declared(self):
        for window in ((0, 0), (0x02200000, 0), (0, 0x1000)):
            with self.assertRaises(ValueError):
                ram.RamTransport.__new__(ram.RamTransport).__init__(None, None, 1, window=window)

    def test_window_may_not_overlap_harness_regions(self):
        """The harness owns the canary, shim, code window and packet."""
        for addr, length in ram.FORBIDDEN:
            for base in (addr - 0x800, addr, addr + length - 4):
                with self.assertRaises(ValueError):
                    ram.RamTransport.__new__(ram.RamTransport).__init__(
                        None, None, 1, window=(base, 0x1000))

    def test_a_pool_window_is_accepted(self):
        transport = ram.RamTransport.__new__(ram.RamTransport)
        transport.window = (0x02200000, 0x140000)
        self.assertTrue(transport._in_window(0x02200000, 4))
        self.assertTrue(transport._in_window(0x0233FFFF - 4, 4))
        self.assertFalse(transport._in_window(0x021FFFFC, 4))
        self.assertFalse(transport._in_window(0x02340000, 4))
        self.assertFalse(transport._in_window(0x02200000, 0))
        self.assertFalse(transport._in_window(0x0233FF00, 0x200))


class BenchManifestTests(unittest.TestCase):
    def test_bench_manifest_is_internally_consistent(self):
        if not BENCH.is_file():
            self.skipTest('bench image not built in this checkout')
        bench = json.loads(BENCH.read_text())
        base, extent = bench['base'], bench['extent']
        # inside the firmware allocator pool, never over the application image
        self.assertGreaterEqual(base, 0x02104000)
        self.assertLessEqual(base + extent, 0x02800000)
        for key in ('entry', 'beacon', 'bss_start', 'bss_end', 'stack_top'):
            self.assertTrue(base <= bench[key] < base + extent, key)
        self.assertLess(bench['bss_start'], bench['bss_end'])
        self.assertLess(bench['bss_end'], bench['stack_top'])
        self.assertEqual(bench['kind'], 'RAM RUN ONLY - NOT FLASHABLE')

    def test_bench_build_never_touches_the_shipping_image(self):
        """The bench build is additive: luma.ram.bin stays the flashable base."""
        manifest = ROOT / 'custom-firmware/build/original/manifest.json'
        if not manifest.is_file():
            self.skipTest('build not present')
        record = json.loads(manifest.read_text())
        self.assertEqual(record['load'], 0x02000000)
        self.assertEqual(record['entry'], 0x02000000)


class FailurePathTests(unittest.TestCase):
    class Fake(dict):
        pass

    def test_run_body_restores_the_window_when_luma_fails(self):
        """A failing run must still put the snapshot back."""
        restored = []

        class T:
            blobs = {}

            def upload(self, address, blob):
                raise dump.DumpError('simulated upload failure')

            def read_region(self, address, length):
                return b'\x00' * length

        def restore_window(t, snapshot):
            restored.append(len(snapshot))
            return True

        report = {}
        snapshot = b'\x11' * 64
        args = type('A', (), {'output': ROOT})()
        with self.assertRaises(dump.DumpError):
            ram.run_bounded(T(), snapshot, 0x02200000, 64, b'\x22' * 32, b'\x33' * 8,
                            b'\x44' * 8, report, args, restore_window)
        self.assertEqual(restored, [64])
        self.assertTrue(report['restore_after_failure'])

    def test_run_bounded_records_a_restore_failure(self):
        """If the restore itself fails that must be visible, not silent."""
        class T:
            blobs = {}

            def upload(self, address, blob):
                raise dump.DumpError('cannot reach the device')

        def restore_window(t, snapshot):
            raise dump.DumpError('bus went away')

        report = {}
        args = type('A', (), {'output': ROOT})()
        with self.assertRaises(dump.DumpError):
            ram.run_bounded(T(), b'\x11' * 8, 0x02200000, 64, b'\x22' * 32, b'\x33' * 8,
                            b'\x44' * 8, report, args, restore_window)
        self.assertIn('bus went away', report['restore_error'])


if __name__ == '__main__':
    unittest.main()
