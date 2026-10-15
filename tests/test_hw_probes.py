"""Offline regression checks for the bench runner; no camera is accessed."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('hw_execute_payload', ROOT / 'docs/hw/execute_payload.py')
hw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hw)


class HardwareProbeTests(unittest.TestCase):
    def run_main(self, argv):
        with patch.object(sys, 'argv', ['execute_payload.py', *argv]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return hw.main()

    def test_camera_defaults_rejected_before_usb(self):
        with patch.object(hw.dump, 'load_usb') as load:
            for payload in ('camprobe', 'camprobe2'):
                with self.assertRaises(SystemExit):
                    self.run_main(['--payload', payload])
            load.assert_not_called()

    def test_long_wait_and_excess_inputs_rejected_before_usb(self):
        with patch.object(hw.dump, 'load_usb') as load:
            for words in ('12,0,0,501', '12,0,0', '12,0,0,-1', '12,0,0,0,0,0,0,0,0'):
                with self.assertRaises(SystemExit):
                    self.run_main(['--payload', 'camprobe2', '--input-words', words])
            load.assert_not_called()

    def test_mode_requires_explicit_safe_target(self):
        with patch.object(hw.dump, 'load_usb') as load:
            for args in ([], ['--input-words', '1,0'], ['--input-words', '3']):
                with self.assertRaises(SystemExit):
                    self.run_main(['--payload', 'mode', *args])
            load.assert_not_called()

    def test_existing_evidence_refused_before_usb(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(hw.dump, 'load_usb') as load:
            with self.assertRaises(FileExistsError):
                self.run_main(['--payload', 'echo', '--output-dir', tmp])
            load.assert_not_called()

    def test_probe_cannot_overwrite_active_ui_stubs(self):
        self.assertEqual(hw.CODE + hw.CODE_LIMIT, hw.BASE)
        self.assertEqual(hw.BASE, 0x020D8800)
        self.assertEqual(hw.BASE + hw.SIZE, 0x020D9000)
        self.assertFalse(hw.BASE <= 0x020D9C00 < hw.BASE + hw.SIZE)
        with tempfile.TemporaryDirectory() as tmp, patch.object(hw.dump, 'load_usb') as load:
            (Path(tmp) / 'echo.bin').write_bytes(bytes(hw.CODE_LIMIT + 1))
            with self.assertRaises(SystemExit):
                self.run_main(['--payload', 'echo', '--payload-dir', tmp])
            load.assert_not_called()

    def test_unknown_frame_hook_refused_before_upload(self):
        sites = {a: b for a, b, _ in (*hw.dump.ROUTINE_SIGNATURES, hw.ICACHE_SITE, hw.FLASH_ID_SITE)}

        class T(hw.PayloadTransport):
            def __init__(self, hook):
                self.hook = hook
                self.writes = []

            def read_region(self, address, length):
                if address == hw.FRAME_HOOK_ADDR:
                    return self.hook[:length]
                return sites[address][:length]

            def write_region(self, *a):
                self.writes.append(a)

        with patch.object(hw.dump, 'check_firmware'):
            with self.assertRaises(hw.dump.DumpError):
                T(bytes.fromhex('deadbeef')).check_firmware()
            stock = T(hw.FRAME_HOOK_STOCK)
            stock.check_firmware()
            self.assertFalse(stock.hoist_active)
            active = T(hw.HOIST_PATCH_BYTES)
            active.check_firmware()
            self.assertTrue(active.hoist_active)

    def test_discovery_failure_still_saved(self):
        with tempfile.TemporaryDirectory() as tmp, \
                patch.object(hw.dump, 'load_usb', side_effect=RuntimeError('no USB')):
            out = Path(tmp) / 'evidence'
            self.assertEqual(self.run_main(['--payload', 'echo', '--output-dir', str(out)]), 1)
            report = json.loads((out / 'report.json').read_text())
            self.assertEqual(report['status'], 'fail')
            self.assertIn('no USB', report['error'])
            self.assertTrue((out / 'input.bin').is_file())
            self.assertTrue((out / 'code.bin').is_file())

    def test_bad_reply_header_fails_and_preserves_reply(self):
        packet = hw.build_packet(seed=0x11223344)
        for field, bad in ((0, 0), (3, 1), (4, 1), (6, 0), (2, 0)):
            reply = bytearray(packet)
            header = [hw.MAGIC, 123, 0x11223344, hw.SIZE, 0, 2, hw.BASE, 0]
            header[field] = bad
            struct.pack_into('<8I', reply, hw.HEADER, *header)
            transport = MagicMock()
            transport.__enter__.return_value = transport
            transport._command.side_effect = [hw.dump.CANARY_PATTERN, hw.dump.CANARY_PATTERN,
                                               packet, bytes(reply)]
            with tempfile.TemporaryDirectory() as tmp, \
                    patch.object(hw.dump, 'load_usb', return_value=(None, None)), \
                    patch.object(hw.dump, 'find_cameras', return_value=([], [])), \
                    patch.object(hw.dump, 'select_camera'), \
                    patch.object(hw.dump, 'device_label', return_value='fake'), \
                    patch.object(hw, 'PayloadTransport', return_value=transport):
                out = Path(tmp) / 'evidence'
                self.assertEqual(self.run_main(['--payload', 'echo', '--output-dir', str(out)]), 1)
                self.assertEqual(json.loads((out / 'report.json').read_text())['status'], 'fail')
                self.assertEqual((out / 'reply.bin').read_bytes(), reply)

    def test_jedec_helper_pin_matches_private_reference(self):
        image = ROOT / 'firmware/original.bin'
        if not image.exists():
            self.skipTest('private firmware dump unavailable')
        address, expected, _ = hw.FLASH_ID_SITE
        self.assertEqual(len(expected), 96)
        offset = address - 0x01FFDA00
        self.assertEqual(image.read_bytes()[offset:offset + len(expected)], expected)

    def test_keep_awake_hoist_only_adds_ram_idle_reset(self):
        import kc02_unlock as u
        ordinary = u.build_hoist_stub()
        bench = u.build_hoist_stub(keep_awake=True)
        self.assertEqual(len(ordinary), 36)
        self.assertEqual(len(bench), u.HOIST_KEEP_AWAKE_LEN)
        self.assertEqual(bench[:12], ordinary[:12])
        self.assertEqual(bench[12:24], u.shim.enc_movhi(3, 0x020c) +
                         u.enc_ori(3, 3, 0xcdcc) + u.shim.enc_sw(3, 0, 0))
        self.assertEqual(u._decode_jal(u.HOIST_STUB_ADDR + 24, bench[24:28]), u.BOT_SERVICE_FN)
        self.assertEqual(bench[28:44], ordinary[16:32])
        self.assertEqual(u._decode_lj(u.HOIST_STUB_ADDR + 44, bench[44:48]), u.PER_FRAME_HOOK_RESUME)
        self.assertEqual([a for _, a, _ in u.hoist_plan(True)],
                         [u.HOIST_STUB_ADDR, u.PER_FRAME_HOOK_ADDR])
        self.assertLessEqual(u.HOIST_STUB_ADDR + len(bench), 0x020d9240)
        import kc02_usb
        ns = kc02_usb.build_parser().parse_args(['usb-hoist', '--keep-awake', '--dry-run'])
        self.assertTrue(ns.keep_awake)
        for _, a, b in u.hoist_plan(True):
            kc02_usb._require_poke_write(a, len(b))
            kc02_usb._require_poke_payload(b)

    def test_unlock_offline_guards(self):
        import kc02_unlock
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(kc02_unlock.selftest(), 0)


if __name__ == '__main__':
    unittest.main()
