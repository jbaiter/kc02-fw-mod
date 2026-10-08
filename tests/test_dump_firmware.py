"""Offline tests; run: python3 -m unittest discover -s tests -v."""

import contextlib
import errno
import importlib.util
import io
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "dump_firmware.py"
spec = importlib.util.spec_from_file_location("dump_firmware", SCRIPT)
dump = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dump)


def image():
    data = bytearray(b"\xff" * dump.FLASH_SIZE)
    data[:512] = bytes(512)
    data[4:8] = b"BLDR"
    data[510:512] = b"\x55\xaa"
    data[12] = (-sum(data[:512])) & 255
    data[0x304:0x308] = b"SFAT"
    return bytes(data)


class UsbError(Exception):
    __module__ = "usb.core"

    def __init__(self, code, number=None):
        super().__init__(f"USB error {code}")
        self.backend_error_code = code
        self.errno = number


class FakeInterface(list):
    bInterfaceNumber = 4
    bAlternateSetting = 0
    bInterfaceClass = 8
    bInterfaceSubClass = 6
    bInterfaceProtocol = 0x50

    def __init__(self):
        super().__init__([
            SimpleNamespace(bEndpointAddress=0x81, bmAttributes=2),
            SimpleNamespace(bEndpointAddress=0x01, bmAttributes=2),
        ])


class FakeConfiguration(list):
    """PyUSB indexes descriptor positions, not bInterfaceNumber values."""

    def __getitem__(self, index):
        if isinstance(index, tuple):
            if index != (0, 0):
                raise ValueError(f"Invalid interface index {index[0]}")
            return super().__getitem__(0)
        return super().__getitem__(index)


class FakeCamera:
    idVendor, idProduct = dump.SUPPORTED_IDS[0]
    bus, address = 1, 2

    def __init__(self):
        self.interface = FakeInterface()
        self.memory = {a: b for a, b, _ in dump.ROUTINE_SIGNATURES}
        self.data = image()
        self.pending = None
        self.queue = []
        self.commands = []
        self.actions = []
        self.active_driver = True
        self.claim_error = None
        self.bad_csw = None
        self.short_writes = False
        self.fragment = None
        self.mismatch = False
        self.interrupt = False
        self.flash_reads = 0

    def get_active_configuration(self):
        return FakeConfiguration([self.interface])

    def is_kernel_driver_active(self, interface):
        if self.active_driver is None:
            raise NotImplementedError
        return self.active_driver

    def detach_kernel_driver(self, interface):
        self.actions.append(("detach", interface))

    def attach_kernel_driver(self, interface):
        self.actions.append(("attach", interface))

    def csw(self, tag):
        fields = [b"USBS", tag, 0, 0]
        if self.bad_csw:
            index, value = self.bad_csw
            fields[index] = value
        return struct.pack("<4sIIB", *fields)

    def write(self, endpoint, data, timeout):
        if self.short_writes:
            return len(data) - 1
        if self.pending:
            tag, address = self.pending
            self.memory[address] = bytes(data)
            self.pending = None
            self.queue.append(self.csw(tag))
            return len(data)
        signature, tag, length, direction, lun, cdb_len = struct.unpack("<4sIIBBB", data[:15])
        opcode, function, address, callback = struct.unpack("<BIII", data[15:28])
        offset = int.from_bytes(data[28:31], "little")
        assert (signature, lun, cdb_len, opcode, function) == (
            b"USBC", 0, 16, 0xCD, dump.MEMORY_TRANSFER_FN
        )
        self.commands.append((address, length, direction, callback, offset))
        if direction == 0:
            self.pending = tag, address
        else:
            if callback == dump.SHIM_ADDR:
                assert self.memory[dump.SHIM_ADDR] == dump.SHIM_BYTES
                self.flash_reads += 1
                if self.interrupt:
                    raise KeyboardInterrupt
                result = self.data[offset:offset + length]
                if self.mismatch and self.flash_reads > dump.FLASH_SIZE // dump.SPI_CHUNK and offset == dump.SPI_CHUNK:
                    result = bytes([result[0] ^ 1]) + result[1:]
            else:
                result = self.memory[address]
            self.queue.extend([result, self.csw(tag)])
        return len(data)

    def read(self, endpoint, length, timeout):
        result = self.queue.pop(0)
        want = min(length, self.fragment or length)
        if len(result) > want:
            self.queue.insert(0, result[want:])
            result = result[:want]
        return result


def fake_usb(camera):
    def claim(dev, interface):
        dev.actions.append(("claim", interface))
        if dev.claim_error:
            raise dev.claim_error

    util = SimpleNamespace(
        claim_interface=claim,
        release_interface=lambda dev, n: dev.actions.append(("release", n)),
        dispose_resources=lambda dev: dev.actions.append(("dispose", None)),
        endpoint_type=lambda attributes: attributes & 3,
        ENDPOINT_TYPE_BULK=2,
    )
    return SimpleNamespace(util=util)


class CommandTests(unittest.TestCase):
    def test_cbw_exact_wire_format(self):
        offset = dump.FLASH_SIZE - dump.SPI_CHUNK
        cbw = dump.build_cbw(42, dump.BUFFER_ADDR, dump.SPI_CHUNK, 0x80, dump.SHIM_ADDR, offset)
        self.assertEqual(len(cbw), 31)
        self.assertEqual(struct.unpack("<4sIIBBB", cbw[:15]), (b"USBC", 42, dump.SPI_CHUNK, 0x80, 0, 16))
        self.assertEqual(struct.unpack("<BIII", cbw[15:28]),
                         (0xCD, dump.MEMORY_TRANSFER_FN, dump.BUFFER_ADDR, dump.SHIM_ADDR))
        self.assertEqual(int.from_bytes(cbw[28:31], "little"), offset)

    def test_safety_guards(self):
        invalid = [
            (0x02000000, 512, 0, dump.CACHE_CLEAN_FN, 0),
            (dump.SHIM_ADDR + 1, 48, 0, dump.CACHE_CLEAN_FN, 0),
            (dump.SHIM_ADDR, 49, 0, dump.CACHE_CLEAN_FN, 0),
            (dump.SHIM_ADDR, 48, 0, dump.CALLBACK_SKIP, 0),
            (dump.SHIM_ADDR, 48, 0, dump.CACHE_CLEAN_FN, 1),
            (dump.BUFFER_ADDR, dump.SPI_CHUNK, 0x80, 0x0203D584, 0),  # sector erase
            (dump.BUFFER_ADDR, dump.SPI_CHUNK, 0x80, 0x0203D38C, 0),  # unadapted SPI
            (dump.BUFFER_ADDR, dump.SPI_CHUNK, 0x80, 0, 0),
            (dump.BUFFER_ADDR, dump.SPI_CHUNK - 1, 0x80, dump.SHIM_ADDR, 0),
            (dump.BUFFER_ADDR, dump.SPI_CHUNK + 16, 0x80, dump.SHIM_ADDR, 0),
            (dump.BUFFER_ADDR, dump.SPI_CHUNK, 0x80, dump.SHIM_ADDR, dump.FLASH_SIZE),
            (dump.BUFFER_ADDR, dump.SPI_CHUNK, 0x80, dump.SHIM_ADDR, -dump.SPI_CHUNK),
            (dump.BUFFER_ADDR, dump.SPI_CHUNK, 0x80, dump.SHIM_ADDR, 1),
            (dump.BUFFER_ADDR, dump.SPI_CHUNK, 0x80, dump.CANARY_ADDR, 1),
            (0x01FFDA04, 4, 0x80, dump.CALLBACK_SKIP, 0),  # faulting mmap probe
            (dump.BUFFER_ADDR, dump.SPI_CHUNK, 0x81, dump.CALLBACK_SKIP, 0),
        ]
        for args in invalid:
            with self.subTest(args=args), self.assertRaises(dump.DumpError):
                dump.build_cbw(1, *args)

    def test_2k_buffer_bounds_and_alignment(self):
        self.assertEqual(dump.SPI_CHUNK, 2048)
        self.assertEqual(len(dump.CANARY_PATTERN), 2048)
        self.assertEqual(dump.BUFFER_ADDR + dump.SPI_CHUNK, 0x020D8900)
        self.assertLess(dump.BUFFER_ADDR + dump.SPI_CHUNK, dump.STAGING_LIMIT)
        self.assertEqual(dump.SPI_CHUNK % 16, 0)
        self.assertEqual(dump.FLASH_SIZE % dump.SPI_CHUNK, 0)

    def test_oversized_staging_transfers_are_rejected(self):
        # A transfer larger than the verified staging region is refused outright.
        with patch.object(dump, "SPI_CHUNK", 8192):
            with self.assertRaisesRegex(dump.DumpError, "verified free region"):
                dump.build_cbw(1, dump.BUFFER_ADDR, 8192, 0x00, dump.CACHE_CLEAN_FN)
        # Inside that region a write is still refused unless it is exactly one of
        # the fixed RAM_BLOBS writes.
        with patch.object(dump, "SPI_CHUNK", 4096):
            with self.assertRaisesRegex(dump.DumpError, "fixed RAM scratch blobs"):
                dump.build_cbw(1, dump.BUFFER_ADDR, 4096, 0x00, dump.CACHE_CLEAN_FN)

    def test_shim_target_and_lengths(self):
        self.assertEqual(len(dump.SHIM_BYTES), 48)
        self.assertEqual(dump.CANARY_BYTES, dump.SHIM_BYTES[-4:])
        word = struct.unpack("<I", dump.SHIM_BYTES[28:32])[0]
        delta = word & 0x3FFFFFF
        if delta & (1 << 25):
            delta -= 1 << 26
        self.assertEqual(dump.SHIM_ADDR + 28 + delta * 4, 0x0203D38C)
        self.assertEqual(word >> 26, 1)  # l.jal


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.camera = FakeCamera()
        self.transport = dump.CameraTransport(fake_usb(self.camera), self.camera, 5000)

    def test_complete_double_read(self):
        with self.transport as dev:
            result = dump.read_dump(dev, quiet=True)
        self.assertEqual(result, self.camera.data)
        self.assertEqual(self.camera.flash_reads, 2 * dump.FLASH_SIZE // dump.SPI_CHUNK)
        self.assertEqual(self.camera.actions, [("detach", 4), ("claim", 4), ("release", 4),
                                              ("attach", 4), ("dispose", None)])
        writes = [(a, n) for a, n, direction, _, _ in self.camera.commands if direction == 0]
        self.assertEqual(writes, [(dump.CANARY_ADDR, 4), (dump.BUFFER_ADDR, dump.SPI_CHUNK), (dump.SHIM_ADDR, 48)])

    def test_trace_usb_payloads_and_execution(self):
        stream = io.StringIO()
        self.transport = dump.CameraTransport(fake_usb(self.camera), self.camera, 5000, trace=True)
        self.assertTrue(self.transport.verbose)
        with contextlib.redirect_stderr(stream), self.transport as dev:
            dev.canary()
            dev.load_shim()
            dev._command(dump.BUFFER_ADDR, dump.SPI_CHUNK, dump.SHIM_ADDR, 0)
        output = stream.getvalue()
        for detail in ("opcode=0xCD", "fn=0x0204C624", "direction=OUT/RAM write",
                       "direction=IN/read", "TX ep=0x01", "RX ep=0x81",
                       "callback=0x0202B058", "cache_clean", "RAM canary: l.jr r9",
                       "SPI-read adapter -> SPI_read@0x0203D38C", "CSW signature=b'USBS'",
                       "residue=0 status=0", dump.SHIM_BYTES.hex(" ")):
            self.assertIn(detail, output)
        self.assertIn(self.camera.data[:dump.SPI_CHUNK].hex(" "), output)

    def test_verbose_logs_metadata_without_rx_payloads(self):
        address = dump.ROUTINE_SIGNATURES[0][0]
        payload = bytes(range(16))
        self.camera.memory[address] = payload
        self.transport.verbose = True
        stream = io.StringIO()
        with contextlib.redirect_stderr(stream), self.transport as dev:
            self.assertEqual(dev.read_memory(address, 16), payload)
        output = stream.getvalue()
        self.assertIn("opcode=0xCD", output)
        self.assertIn("TX ep=0x01", output)
        self.assertIn("RX ep=0x81 16 bytes\n", output)
        self.assertIn("CSW signature=b'USBS'", output)
        self.assertNotIn(payload.hex(" "), output)
        self.assertNotRegex(output, r"RX ep=.* bytes:")

    def test_trace_is_off_by_default(self):
        stream = io.StringIO()
        with contextlib.redirect_stderr(stream), self.transport as dev:
            dev.read_memory(dump.ROUTINE_SIGNATURES[0][0], 16)
        self.assertEqual(stream.getvalue(), "")

    def test_bad_csw_is_logged_before_rejection(self):
        self.transport.verbose = True
        self.camera.bad_csw = (3, 1)
        stream = io.StringIO()
        with contextlib.redirect_stderr(stream), self.transport as dev:
            with self.assertRaises(dump.DumpError):
                dev.read_memory(dump.ROUTINE_SIGNATURES[0][0], 16)
        self.assertIn("residue=0 status=1", stream.getvalue())

    def test_unknown_firmware_no_ram_writes(self):
        self.camera.memory[dump.ROUTINE_SIGNATURES[0][0]] = bytes(16)
        with self.transport as dev, self.assertRaisesRegex(dump.DumpError, "Unsupported firmware"):
            dump.read_dump(dev, quiet=True)
        self.assertFalse(any(command[2] == 0 for command in self.camera.commands))

    def test_second_read_mismatch(self):
        self.camera.mismatch = True
        with self.transport as dev, self.assertRaisesRegex(dump.DumpError, f"first mismatch at 0x{dump.SPI_CHUNK:06X}"):
            dump.read_dump(dev, quiet=True)

    def test_claim_failure_restores_driver(self):
        self.camera.claim_error = UsbError(-3)
        with self.assertRaises(UsbError):
            with self.transport:
                self.fail("claim should fail")
        self.assertEqual(self.camera.actions, [("detach", 4), ("claim", 4), ("attach", 4), ("dispose", None)])

    def test_unsupported_driver_query_is_normal(self):
        self.camera.active_driver = None
        with self.transport:
            pass
        self.assertEqual(self.camera.actions, [("claim", 4), ("release", 4), ("dispose", None)])

    def test_interface_number_is_not_descriptor_position(self):
        config = self.camera.get_active_configuration()
        self.assertEqual(config[(0, 0)].bInterfaceNumber, 4)
        with self.assertRaisesRegex(ValueError, "Invalid interface index 4"):
            config[(4, 0)]
        with self.transport:
            self.assertTrue(self.transport.claimed)

    def test_missing_interface(self):
        self.camera.interface.bInterfaceNumber = 0
        with self.assertRaisesRegex(dump.DumpError, "no mass-storage interface 4"):
            with self.transport:
                pass
        self.assertEqual(self.camera.actions, [("dispose", None)])

    def test_invalid_descriptors(self):
        self.camera.interface.pop()
        with self.assertRaisesRegex(dump.DumpError, "Unexpected interface"):
            with self.transport:
                pass
        self.assertEqual(self.camera.actions, [("dispose", None)])

    def test_invalid_csw_fields(self):
        for bad in [(0, b"FAIL"), (1, 999), (2, 1), (3, 1)]:
            self.camera.bad_csw = bad
            with self.subTest(bad=bad), self.transport as dev:
                with self.assertRaisesRegex(dump.DumpError, "Invalid USB command status"):
                    dev.read_memory(dump.ROUTINE_SIGNATURES[0][0], 16)

    def test_short_and_fragmented_transfers(self):
        with self.transport as dev:
            self.camera.short_writes = True
            with self.assertRaisesRegex(dump.DumpError, "Short USB write"):
                dev.read_memory(dump.ROUTINE_SIGNATURES[0][0], 16)
            self.camera.short_writes = False
            self.camera.fragment = 5
            self.assertEqual(dev.read_memory(dump.ROUTINE_SIGNATURES[0][0], 16), dump.ROUTINE_SIGNATURES[0][1])

    def test_zero_byte_usb_read(self):
        with self.transport as dev, patch.object(self.camera, "read", return_value=b""):
            with self.assertRaisesRegex(dump.DumpError, "Short USB read"):
                dev.read_memory(dump.ROUTINE_SIGNATURES[0][0], 16)

    def test_payload_guard(self):
        with self.transport as dev, self.assertRaisesRegex(dump.DumpError, "RAM payload differs"):
            dev._command(dump.SHIM_ADDR, 48, dump.CACHE_CLEAN_FN, payload=bytes(48))
        self.assertEqual(self.camera.commands, [])

    def test_scratch_readback_mismatch(self):
        with self.transport as dev, patch.object(dev, "read_memory", return_value=bytes(48)):
            with self.assertRaisesRegex(dump.DumpError, "RAM scratch verification failed"):
                dev.load_shim()

    def test_canary_mismatch_stops_before_shim(self):
        with self.transport as dev, patch.object(dev, "canary", return_value=bytes(dump.SPI_CHUNK)):
            with self.assertRaisesRegex(dump.DumpError, "RAM execution check failed"):
                dump.read_dump(dev, quiet=True)
        self.assertFalse(any(c[0] == dump.SHIM_ADDR for c in self.camera.commands))


class TerminalStream(io.StringIO):
    @property
    def encoding(self):
        return "utf-8"

    def isatty(self):
        return True


class ProgressTests(unittest.TestCase):
    def test_unicode_bar_and_completion(self):
        stream = TerminalStream()
        with contextlib.redirect_stderr(stream), \
                patch.object(dump.time, "monotonic", side_effect=[100, 101, 102]):
            with dump.Progress("📥 Dumping") as progress:
                progress(512, 1024)
                progress(1024, 1024)
                self.assertNotIn("✅ Dumping", stream.getvalue())
        output = stream.getvalue()
        self.assertIn("▰" * 10 + "▱" * 10, output)
        self.assertIn("▰" * 20, output)
        self.assertIn(" 50%", output)
        self.assertIn("100%", output)
        self.assertIn("KiB/s", output)
        self.assertIn("ETA 00:01", output)
        self.assertIn("in 00:02", output)
        self.assertNotIn("\033", output)
        self.assertNotIn("[", output)
        self.assertTrue(output.endswith("\n"))
        self.assertIn("✅ Dumping", output)
        self.assertEqual(output.count("\n"), 1)

    def test_regular_logs_only_show_final_result(self):
        stream = io.StringIO()
        with contextlib.redirect_stderr(stream), dump.Progress("📥 Dumping") as progress:
            for done in range(0, 1001):
                progress(done, 1000)
        output = stream.getvalue()
        self.assertEqual(len(output.splitlines()), 1)
        self.assertTrue(output.startswith("✅ Dumping"))
        self.assertNotIn("\r", output)
        self.assertIn("100%", output)

    def test_verbose_non_tty_logs_are_sparse(self):
        stream = io.StringIO()
        with contextlib.redirect_stderr(stream), dump.Progress("📥 Dumping", verbose=True) as progress:
            for done in range(0, 1001):
                progress(done, 1000)
        output = stream.getvalue()
        self.assertEqual(len(output.splitlines()), 13)
        self.assertEqual(len(re.findall(r"[▰▱]{20}", output)), 12)
        self.assertNotIn("\r", output)
        self.assertNotIn("\033", output)
        self.assertIn("100%", output)

    def test_refresh_is_throttled_but_finish_is_immediate(self):
        stream = TerminalStream()
        with contextlib.redirect_stderr(stream), \
                patch.object(dump.time, "monotonic", side_effect=[100, 100, 100.04, 100.2, 100.21]):
            with dump.Progress("📥 Dumping") as progress:
                progress(100, 1000)
                progress(200, 1000)  # Too soon to redraw.
                progress(300, 1000)
                progress(1000, 1000)  # Completion always redraws.
        self.assertEqual(stream.getvalue().count("\r"), 5)
        self.assertNotIn(" 20%", stream.getvalue())
        self.assertTrue(stream.getvalue().endswith("\n"))

    def test_status_replaces_the_active_line(self):
        stream = TerminalStream()
        with contextlib.redirect_stderr(stream):
            with dump.Status("🔍 Checking compatibility", success="✅ Compatibility checked."):
                self.assertEqual(stream.getvalue().count("\n"), 0)
        self.assertIn("\r✅ Compatibility checked.", stream.getvalue())
        self.assertEqual(stream.getvalue().count("\n"), 1)

    def test_failure_never_marks_a_completed_read_successful(self):
        stream = TerminalStream()
        with contextlib.redirect_stderr(stream), self.assertRaises(dump.DumpError):
            with dump.Progress("📥 Validating") as progress:
                progress(100, 100)
                raise dump.DumpError("mismatch")
        self.assertIn("❌ Validating failed.", stream.getvalue())
        self.assertNotIn("✅", stream.getvalue())
        self.assertEqual(stream.getvalue().count("\n"), 1)

    def test_quiet_and_empty_total(self):
        stream = TerminalStream()
        with contextlib.redirect_stderr(stream):
            dump.Progress("📥 Dumping", True)(100, 100)
            dump.Progress("📥 Dumping", False)(0, 0)
        self.assertEqual(stream.getvalue(), "")


class CliTests(unittest.TestCase):
    def test_standalone_without_support_modules_or_usb(self):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "dump_firmware.py"
            shutil.copyfile(SCRIPT, script)
            result = subprocess.run([sys.executable, "-S", str(script), "--help"],
                                    capture_output=True, text=True, cwd=directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([sys.executable, "-S", str(script)],
                                    capture_output=True, text=True, cwd=directory)
            self.assertEqual(result.returncode, 1)
            self.assertIn("PyUSB is missing", result.stderr)
            self.assertNotIn("Traceback", result.stderr)
            self.assertFalse((Path(directory) / "firmware/original.bin").exists())
            self.assertEqual(list((Path(directory) / "firmware").iterdir()), [])

    def test_setup_removed_and_sd_card_optional(self):
        parser = dump.build_parser()
        self.assertNotIn("--setup", parser.format_help())
        self.assertIn("SD card can stay inserted", " ".join(parser.format_help().split()))
        with contextlib.redirect_stderr(io.StringIO()) as stderr, self.assertRaises(SystemExit) as exit:
            parser.parse_args(["--setup"])
        self.assertEqual(exit.exception.code, 2)
        self.assertIn("❌", stderr.getvalue())

    def test_readme_documents_all_platforms(self):
        readme = (SCRIPT.parent.parent / "README.md").read_text()
        for expected in ("## USB device permissions and drivers", "### Linux", "### macOS",
                         "### Windows", "70-kc02.rules", "brew install libusb", "WinUSB"):
            self.assertIn(expected, readme)
        self.assertNotIn("--setup", readme)

    def test_device_selection(self):
        camera = FakeCamera()
        for args, message in [(([], [], None), "No KC02"), (([], [camera], None), "webcam-only"),
                              (([camera, camera], [], None), "Found 2"), (([camera], [], 2), "not found")]:
            with self.assertRaisesRegex(dump.DumpError, message):
                dump.select_camera(*args)
        self.assertIs(dump.select_camera([camera], [], 1), camera)

    def test_usb_error_messages(self):
        for code, message in [(-3, "permission denied"), (-6, "busy"), (-4, "disconnected"),
                              (-7, "timeout"), (-12, "not supported"), (-9, "stalled")]:
            self.assertIn(message, dump.explain_error(UsbError(code)))
        self.assertIn("Not enough disk space", dump.explain_error(OSError(errno.ENOSPC, "full")))

    def test_invalid_image(self):
        data = image()
        dump.validate_image(data)
        for offset in (4, 12, 510, 0x304):
            bad = bytearray(data)
            bad[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(dump.DumpError):
                dump.validate_image(bad)
        with self.assertRaisesRegex(dump.DumpError, "Incomplete"):
            dump.validate_image(data[:-1])

    def run_fake_main(self, camera, *args):
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch.object(dump, "load_usb", return_value=(fake_usb(camera), object())), \
                patch.object(dump, "find_cameras", return_value=([camera], [])), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = dump.main(list(args))
        return code, stdout.getvalue(), stderr.getvalue()

    def test_save_no_clobber_and_force(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "original.bin"
            code, out, err = self.run_fake_main(FakeCamera(), "-q", "--single-read", "-o", str(path))
            self.assertEqual(code, 0, err)
            self.assertIn("✅ Saved", out)
            self.assertNotIn("SHA-256:", out)
            self.assertEqual(path.read_bytes(), image())
            code, _, err = self.run_fake_main(FakeCamera(), "-o", str(path))
            self.assertEqual(code, 1)
            self.assertIn("already exists", err)
            path.write_bytes(b"earlier backup")
            camera = FakeCamera()
            camera.interrupt = True
            code, _, err = self.run_fake_main(camera, "-q", "--force", "-o", str(path))
            self.assertEqual(code, 130, err)
            self.assertEqual(path.read_bytes(), b"earlier backup")
            self.assertEqual(list(Path(directory).iterdir()), [path])
            code, _, err = self.run_fake_main(FakeCamera(), "-q", "--single-read", "--force", "-o", str(path))
            self.assertEqual(code, 0, err)
            self.assertEqual(path.read_bytes(), image())

    def test_regular_output_is_compact_and_failures_are_not_successes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "original.bin")
            code, out, err = self.run_fake_main(FakeCamera(), "-o", path)
            self.assertEqual(code, 0, err)
            lines = err.splitlines()
            self.assertEqual(len(lines), 5)
            for line, prefix in zip(lines, ("📷 Camera:", "🔌 Keep it connected until reading finishes.",
                                           "✅ Compatibility checked.", "✅ Dumping", "✅ Validating")):
                self.assertTrue(line.startswith(prefix), line)
            self.assertTrue(out.startswith("✅ Saved"))
            self.assertEqual(len(out.splitlines()), 1)
            for internal in ("RAM", "SPI", "opcode", "🔧", "SHA-256"):
                self.assertNotIn(internal, out + err)
            camera = FakeCamera()
            camera.mismatch = True
            code, _, err = self.run_fake_main(camera, "--force", "-o", path)
            self.assertEqual(code, 1)
            self.assertIn("❌ Validating failed.", err)
            self.assertIn("❌ Error:", err)
            self.assertNotIn("✅ Validating", err)

    def test_verbose_mode_includes_ram_check_and_diagnostics(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "original.bin")
            with patch.object(dump.CameraTransport, "flash_read", return_value=image()):
                code, out, err = self.run_fake_main(FakeCamera(), "--verbose", "-o", path)
            self.assertEqual(code, 0, err)
            for detail in ("Checking RAM Execution", "Loading SPI-read adapter", "opcode=0xCD",
                           "TX ep=0x01", "RX ep=0x81", "Requesting camera execution:"):
                self.assertIn(detail, err)
            self.assertIn("🔐 SHA-256:", out)
            self.assertIn("💾 Saving", out)
            self.assertIn("✅ Saved", out)

    def test_verbosity_levels(self):
        parser = dump.build_parser()
        self.assertEqual(parser.parse_args([]).verbosity, 0)
        self.assertEqual(parser.parse_args(["-v"]).verbosity, 1)
        self.assertEqual(parser.parse_args(["--trace"]).verbosity, 2)
        for flags in (["-v", "-q"], ["--trace", "-q"]):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                parser.parse_args(flags)

    def test_trace_cli_enables_verbose_and_raw_rx(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "original.bin")
            with patch.object(dump.CameraTransport, "flash_read", return_value=image()):
                code, out, err = self.run_fake_main(FakeCamera(), "--trace", "-o", path)
            self.assertEqual(code, 0, err)
            self.assertIn("Checking RAM Execution", err)
            self.assertRegex(err, r"RX ep=0x81 16 bytes: [0-9a-f ]+")
            self.assertIn("🔐 SHA-256:", out)

    def test_safe_publication_on_fat_and_races(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "original.bin"
            staged = dump.prepare_output(path, False)
            try:
                with patch.object(dump.os, "link", side_effect=OSError(errno.EOPNOTSUPP, "FAT")):
                    dump.save_dump(staged, path, image(), False)
                self.assertEqual(path.read_bytes(), image())
            finally:
                staged.close()
                Path(staged.name).unlink(missing_ok=True)
            path.unlink()
            staged = dump.prepare_output(path, False)
            try:
                path.write_bytes(b"created by another process")
                with self.assertRaisesRegex(dump.DumpError, "appeared during dumping"):
                    dump.save_dump(staged, path, image(), False)
                self.assertEqual(path.read_bytes(), b"created by another process")
            finally:
                staged.close()
                Path(staged.name).unlink(missing_ok=True)

    def test_disk_full_preserves_backup_and_cleans_temporary_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "original.bin"
            path.write_bytes(b"earlier backup")
            with patch.object(dump.os, "fsync", side_effect=OSError(errno.ENOSPC, "full")):
                code, _, err = self.run_fake_main(FakeCamera(), "-q", "--single-read", "--force", "-o", str(path))
            self.assertEqual(code, 1)
            self.assertIn("Not enough disk space", err)
            self.assertEqual(path.read_bytes(), b"earlier backup")
            self.assertEqual(list(Path(directory).iterdir()), [path])

    def test_partial_usb_dump_leaves_no_output_or_temporary_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "original.bin"
            camera = FakeCamera()
            write = camera.write

            def disconnect_after_first_chunk(endpoint, data, timeout):
                if camera.flash_reads:
                    raise UsbError(-4, errno.ENODEV)
                return write(endpoint, data, timeout)

            with patch.object(camera, "write", side_effect=disconnect_after_first_chunk):
                code, _, err = self.run_fake_main(camera, "-q", "-o", str(path))
            self.assertEqual(code, 1)
            self.assertIn("disconnected", err)
            self.assertFalse(path.exists())
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_partial_image_cannot_be_saved(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "original.bin"
            with patch.object(dump.CameraTransport, "flash_read", return_value=image()[:512]):
                code, _, err = self.run_fake_main(FakeCamera(), "-q", "-o", str(path))
            self.assertEqual(code, 1)
            self.assertIn("Incomplete dump", err)
            self.assertFalse(path.exists())
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_partial_fat_output_is_removed_on_error_or_interrupt(self):
        for failure in (OSError(errno.ENOSPC, "full"), KeyboardInterrupt()):
            with self.subTest(failure=type(failure).__name__), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "original.bin"
                staged = dump.prepare_output(path, False)
                try:
                    # First fsync completes staging, second fails after creating output.
                    with patch.object(dump.os, "link", side_effect=OSError(errno.EOPNOTSUPP, "FAT")), \
                            patch.object(dump.os, "fsync", side_effect=[None, failure]):
                        with self.assertRaises(type(failure)):
                            dump.save_dump(staged, path, image(), False)
                    self.assertFalse(path.exists())
                finally:
                    staged.close()
                    Path(staged.name).unlink(missing_ok=True)
                self.assertEqual(list(Path(directory).iterdir()), [])

    def test_short_fat_write_is_removed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "original.bin"
            staged = dump.prepare_output(path, False)
            open_file = Path.open

            class ShortFile:
                def __enter__(self):
                    self.raw = open_file(path, "xb", buffering=0)
                    return self

                def write(self, data):
                    return self.raw.write(data[:512])

                def __exit__(self, *args):
                    self.raw.close()

            try:
                with patch.object(dump.os, "link", side_effect=OSError(errno.EOPNOTSUPP, "FAT")), \
                        patch.object(Path, "open", return_value=ShortFile()):
                    with self.assertRaisesRegex(OSError, "short write to output dump"):
                        dump.save_dump(staged, path, image(), False)
                self.assertFalse(path.exists())
            finally:
                staged.close()
                Path(staged.name).unlink(missing_ok=True)

    def test_unwritable_output_fails_before_usb(self):
        with patch.object(dump.tempfile, "NamedTemporaryFile", side_effect=PermissionError(errno.EACCES, "denied")), \
                patch.object(dump, "load_usb") as load_usb, contextlib.redirect_stderr(io.StringIO()) as stderr:
            code = dump.main(["-o", "backup.bin"])
        self.assertEqual(code, 1)
        self.assertIn("permission denied", stderr.getvalue())
        load_usb.assert_not_called()

    def test_destbin_rejected(self):
        with self.assertRaisesRegex(dump.DumpError, "firmware update"):
            dump.prepare_output(Path("DestBin.bin"), True)

    def test_list_never_claims(self):
        camera = FakeCamera()
        code, out, err = self.run_fake_main(camera, "--list")
        self.assertEqual(code, 0, err)
        self.assertEqual(out, "📷 1: 0219:3280 (bus 1, address 2) [✅ supports dumping]\n")
        self.assertEqual(camera.actions, [])


if __name__ == "__main__":
    unittest.main()
