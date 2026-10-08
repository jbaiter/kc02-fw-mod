"""Offline image guards and an independent, no-delay-slot OR1K execution model."""

import contextlib
import errno
import hashlib
import importlib.util
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "child_mode", Path(__file__).resolve().parents[1] / "tools/patch_child_mode.py"
)
child = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(child)

# Stock ui_mode_request, independently read from the reference Ghidra listing.
MODE_REQUEST_BYTES = bytes.fromhex(
    "f80fe1d7fc17e1d70b0043bcf8ff219c180000100c02a01850cda5a8"
    "0000c584010006bc130000100c0240180200c3b85ccd42a80010c6e0"
    "0000c684000006bc09000010042006d40c02801854cd84a80000c484"
    "0c0280184ccd84a8003004d4030000000b0023bc02000010001805d4"
    "0800219c f8ff2184 fcff4184 00480044"
)


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


class CPU:
    """Execute actual encoded words, not the builder's assembly descriptions."""

    def __init__(self, power_bytes):
        self.memory = {}
        self.r = [(0x12340000 + i) for i in range(32)]
        self.r[0] = 0
        self.r[1] = 0x027FF000
        self.r[9] = child.PRIMARY_RETURN
        self.r[4] = 1
        self.r[5] = 0x020D1000
        self.flag = False
        self.writes = []
        self.put(child.POWER, power_bytes)
        self.put(child.MODE_REQUEST, MODE_REQUEST_BYTES)
        self.store(self.r[5], 0, 4)
        self.store(0x020CCD50, 9, 4)  # pending
        self.store(0x020CCD54, 3, 4)  # current
        self.store(0x020CCD4C, 0, 4)  # previous
        self.store(0x020CCD5C + 6 * 4, 0x020C4C18, 4)
        self.store(0x020C4C18 + 4, 0xAABBCCDD, 4)
        self.writes.clear()

    def put(self, address, data):
        self.memory.update((address + i, b) for i, b in enumerate(data))

    def load(self, address, size):
        return sum(self.memory.get(address + i, 0) << (i * 8) for i in range(size))

    def store(self, address, value, size):
        self.put(address, (value & ((1 << (size * 8)) - 1)).to_bytes(size, "little"))
        self.writes.append((address, size))

    def run(self, pc=child.POWER, max_steps=500):
        for _ in range(max_steps):
            if pc == child.PRIMARY_RETURN:
                return
            w = self.load(pc, 4)
            op, d, a, b = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
            imm = signed(w & 0xFFFF, 16)
            next_pc = pc + 4
            if op in (0, 1, 3, 4):
                target = (pc + signed(w & 0x03FFFFFF, 26) * 4) & 0xFFFFFFFF
                if (
                    op in (0, 1)
                    or (op == 3 and not self.flag)
                    or (op == 4 and self.flag)
                ):
                    next_pc = target
                if op == 1:
                    self.r[9] = pc + 4
            elif op == 0x11:  # jr
                next_pc = self.r[b]
            elif op == 6:
                self.r[d] = (w & 0xFFFF) << 16
            elif op == 0x27:
                self.r[d] = (self.r[a] + imm) & 0xFFFFFFFF
            elif op == 0x2A:
                self.r[d] = self.r[a] | (w & 0xFFFF)
            elif op == 0x2E:  # slli (needed by stock mode request)
                self.r[d] = (self.r[a] << (w & 31)) & 0xFFFFFFFF
            elif op == 0x38:  # add
                if w & 0x7FF:
                    raise AssertionError(f"Unsupported ALU word {w:08x}")
                self.r[d] = (self.r[a] + self.r[b]) & 0xFFFFFFFF
            elif op == 0x2F:
                rhs = imm & 0xFFFFFFFF
                self.flag = {
                    0: self.r[a] == rhs,
                    1: self.r[a] != rhs,
                    2: self.r[a] > rhs,
                    5: self.r[a] <= rhs,
                }[d]
            elif op in (0x21, 0x23, 0x25):
                self.r[d] = self.load(
                    (self.r[a] + imm) & 0xFFFFFFFF, {0x21: 4, 0x23: 1, 0x25: 2}[op]
                )
            elif op in (0x35, 0x36):
                displacement = signed(((w >> 10) & 0xF800) | (w & 0x7FF), 16)
                self.store(
                    (self.r[a] + displacement) & 0xFFFFFFFF,
                    self.r[b],
                    4 if op == 0x35 else 1,
                )
            else:
                raise AssertionError(f"Unsupported instruction {w:08x} at {pc:08x}")
            self.r[0] = 0
            pc = next_pc
        raise AssertionError("Execution did not return within the instruction budget")


class ExecutionTests(unittest.TestCase):
    def patched(self):
        return CPU(child.patch_sites()[2][3])

    def assert_preserved(self, cpu, before):
        for reg in (0, 1, 2, 8, 9, 10, *range(12, 32)):
            self.assertEqual(cpu.r[reg], before[reg], f"r{reg}")
        self.assertEqual(cpu.r[11], 0)

    def test_locked_power_does_not_change_mode_or_camera_state(self):
        cpu = self.patched()
        before = cpu.r.copy()
        cpu.run()
        self.assert_preserved(cpu, before)
        self.assertEqual(cpu.load(0x020CCD50, 4), 9)
        self.assertEqual(cpu.load(0x020D41D4, 1), 0)
        self.assertEqual(cpu.writes, [])

    def test_allowed_power_matches_stock_side_effects_and_register_preservation(self):
        for state in (0, 1, 2, 255):
            for pending in (1, 3, 9):
                with self.subTest(state=state, pending=pending):
                    stock, cpu = CPU(child.STOCK_POWER), self.patched()
                    for machine in (stock, cpu):
                        machine.store(0x020D41D4, state, 1)
                        machine.store(0x020D41D5, 7, 1)
                        machine.store(0x020CCD50, pending, 4)
                    cpu.store(child.CONTROL, 1, 1)
                    before = cpu.r.copy()
                    stock.run()
                    cpu.run()
                    self.assert_preserved(cpu, before)
                    for address, size in (
                        (0x020CCD50, 4),
                        (0x020CCD4C, 4),
                        (0x020C4C1C, 4),
                        (0x020D41D4, 1),
                        (0x020D41D5, 1),
                    ):
                        self.assertEqual(
                            cpu.load(address, size), stock.load(address, size)
                        )
                    self.assertEqual(cpu.load(child.CONTROL, 1), 0)

    def test_arm_is_one_shot(self):
        cpu = self.patched()
        cpu.store(child.CONTROL, 1, 1)
        cpu.run()
        self.assertEqual(cpu.load(0x020CCD50, 4), 6)
        cpu.store(0x020CCD50, 3, 4)
        cpu.r[4], cpu.r[5] = 1, 0x020D1000  # fresh dispatcher arguments
        cpu.run()
        self.assertEqual(cpu.load(0x020CCD50, 4), 3)

    def test_adult_override_survives_consumption_of_arm(self):
        cpu = self.patched()
        cpu.store(child.CONTROL, 0x0101, 2)
        cpu.run()
        self.assertEqual(cpu.load(child.CONTROL, 2), 0x0100)
        self.assertEqual(cpu.load(0x020CCD50, 4), 6)
        cpu.store(0x020CCD50, 3, 4)
        cpu.r[4], cpu.r[5] = 1, 0x020D1000
        cpu.run()
        self.assertEqual(cpu.load(0x020CCD50, 4), 6)

    def test_stock_flag_and_payload_filters_do_not_consume_arm(self):
        for flag, payload in ((0, 0), (2, 0), (1, 1), (1, 2)):
            with self.subTest(flag=flag, payload=payload):
                cpu = self.patched()
                cpu.r[4] = flag
                cpu.store(cpu.r[5], payload, 4)
                cpu.store(child.CONTROL, 1, 1)
                before = cpu.r.copy()
                cpu.run()
                self.assert_preserved(cpu, before)
                self.assertEqual(cpu.load(child.CONTROL, 1), 1)
                self.assertEqual(cpu.load(0x020CCD50, 4), 9)

    def test_up_trampoline_preserves_displaced_instruction_and_arguments(self):
        cpu = self.patched()
        cpu.put(child.UP, child.patch_sites()[1][3])
        # Stop after returning to the untouched stock body. The actual saved-r18
        # store must have happened, with no stack/argument/link changes.
        sentinel = bytes.fromhex("00480044")
        cpu.put(child.UP + 4, sentinel)
        before = cpu.r.copy()
        cpu.run(child.UP)
        self.assertEqual(cpu.load(child.CONTROL, 1), 1)
        self.assertEqual(cpu.load(before[1] - 12, 4), before[18])
        self.assertEqual(cpu.r, before)
        self.assertEqual(cpu.writes[-2:], [(child.CONTROL, 1), (before[1] - 12, 4)])

    def test_payload_matches_independent_ghidra_roundtrip(self):
        expected = bytes.fromhex(
            "010004bc1200000c00008584000024bc0f000010cc24c994000006bc"
            "0c000010cc0489d80d02c018d441e68c000027bc04000010d50106d9"
            "0200e09cd43906d90600609c0000609d6ef8ff030000609d00480044"
            "cc2489d8f497e1d74cfeff0300000000"
        )
        self.assertEqual(child.patch_sites()[2][3], expected)

    def test_control_word_is_data_not_on_an_execution_path(self):
        self.assertEqual(child.CONTROL, child.POWER + 96)
        self.assertEqual(child.ARM, child.POWER + 84)
        self.assertEqual(child.CONTROL_OFFSET, 0x24CC)
        self.assertEqual(child.patch_sites()[2][3][-4:], b"\0" * 4)
        self.assertEqual(child.patch_sites()[1][3], bytes.fromhex("b3010000"))


class ImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Synthetic fixture: no copyrighted full image is needed for unit tests.
        image = bytearray(child.FLASH_SIZE)
        image[4:8] = b"BLDR"
        image[0x1FE:0x200] = b"\x55\xaa"
        image[0x1FD] = (-sum(image[:512])) & 255
        image[0x304:0x308] = b"SFAT"
        for _, offset, stock, _ in child.patch_sites():
            image[offset : offset + len(stock)] = stock
        cls.image = bytes(image)
        cls.boot_hash = hashlib.sha256(image[: child.APP_START]).hexdigest()
        cls.app_hash = hashlib.sha256(
            image[child.APP_START : child.APP_END]
        ).hexdigest()

    def setUp(self):
        self.hashes = patch.multiple(
            child, BOOT_SHA256=self.boot_hash, APP_SHA256=self.app_hash
        )
        self.hashes.start()
        self.addCleanup(self.hashes.stop)

    def test_apply_restore_and_idempotency(self):
        result = child.patch_image(self.image)
        self.assertEqual(child.patch_image(result), result)
        self.assertEqual(child.patch_image(result, restore=True), self.image)
        self.assertEqual(child.patch_image(self.image, restore=True), self.image)

    def test_boot_only_rehearsal_changes_one_byte_and_upgrades_to_full(self):
        rehearsal = child.patch_image(self.image, boot_only=True)
        changed = [i for i, (a, b) in enumerate(zip(self.image, rehearsal)) if a != b]
        self.assertEqual(changed, [0x2AAC])
        self.assertEqual(child.patch_image(rehearsal), child.patch_image(self.image))
        self.assertEqual(child.patch_image(rehearsal, restore=True), self.image)
        self.assertEqual(
            child.patch_image(child.patch_image(self.image), boot_only=True), rehearsal
        )

    def test_stage_arguments_are_mutually_exclusive(self):
        with self.assertRaises(child.PatchError):
            child.patch_image(self.image, restore=True, boot_only=True)

    def test_changes_stay_inside_exact_three_spans(self):
        result = child.patch_image(self.image)
        allowed = set()
        for _, offset, stock, _ in child.patch_sites():
            allowed.update(range(offset, offset + len(stock)))
        changed = {i for i, (a, b) in enumerate(zip(self.image, result)) if a != b}
        self.assertTrue(changed)
        self.assertLessEqual(changed, allowed)
        self.assertEqual(len(allowed), 108)
        self.assertEqual(result[: child.APP_START], self.image[: child.APP_START])
        self.assertEqual(result[child.APP_END :], self.image[child.APP_END :])

    def test_themes_state_record_and_erased_tail_are_preserved(self):
        image = bytearray(self.image)
        for offset in (0xD3510, 0x1A6102, 0x30701C, 0x3FFFFF):
            image[offset] = 17
        image = bytes(image)
        result = child.patch_image(image)
        self.assertEqual(child.patch_image(result, restore=True), image)
        self.assertEqual(result[child.APP_END :], image[child.APP_END :])

    def test_unknown_application_and_boot_bytes_are_refused(self):
        for offset in (0x1000, 0x2700, 0x7EC40, 0xC869C):
            with self.subTest(offset=offset):
                image = bytearray(self.image)
                image[offset] ^= 1
                with self.assertRaisesRegex(child.PatchError, "Unsupported"):
                    child.patch_image(image)

    def test_bad_sizes_and_headers_are_refused(self):
        for image in (self.image[:-1], self.image + b"x", b"\0" * child.FLASH_SIZE):
            with self.assertRaises(child.PatchError):
                child.patch_image(image)

    def test_corrupt_hook_and_partial_patch_are_refused(self):
        image = bytearray(self.image)
        _, offset, _, patched = child.patch_sites()[1]
        image[offset : offset + len(patched)] = patched
        with self.assertRaisesRegex(child.PatchError, "Partial/mixed"):
            child.patch_image(image)
        image = bytearray(self.image)
        image[child.file_offset(child.UP)] ^= 1
        with self.assertRaisesRegex(child.PatchError, "Unexpected bytes"):
            child.patch_image(image)

    def test_loaded_lut_is_not_used_as_storage(self):
        result = child.patch_image(self.image)
        self.assertEqual(result[0x7EC40:0x80268], self.image[0x7EC40:0x80268])

    def test_input_cannot_be_output_even_when_forced(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "original.bin"
            source.write_bytes(self.image)
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(
                    child.main(["-i", str(source), "-o", str(source), "--force"]), 1
                )
            self.assertEqual(source.read_bytes(), self.image)

    def test_dry_run_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = (
                Path(directory) / "source.bin",
                Path(directory) / "new/DestBin.bin",
            )
            source.write_bytes(self.image)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(
                    child.main(["-i", str(source), "-o", str(output), "--dry-run"]), 0
                )
            self.assertFalse(output.parent.exists())


class OutputTests(unittest.TestCase):
    def test_no_clobber_force_and_staging_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "DestBin.bin"
            child.save_image(output, b"first")
            with self.assertRaises(FileExistsError):
                child.save_image(output, b"second")
            self.assertEqual(output.read_bytes(), b"first")
            child.save_image(output, b"second", force=True)
            self.assertEqual(output.read_bytes(), b"second")
            self.assertEqual(list(output.parent.iterdir()), [output])

    def test_fat_fallback_preserves_existing_files(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "DestBin.bin"
            with patch.object(
                child.os, "link", side_effect=OSError(errno.EOPNOTSUPP, "FAT")
            ):
                child.save_image(output, b"first")
                with self.assertRaises(FileExistsError):
                    child.save_image(output, b"second")
            self.assertEqual(output.read_bytes(), b"first")
            self.assertEqual(list(output.parent.iterdir()), [output])

    def test_failed_staging_does_not_replace_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "DestBin.bin"
            output.write_bytes(b"existing")
            with (
                patch.object(child.os, "fsync", side_effect=OSError("disk full")),
                self.assertRaises(OSError),
            ):
                child.save_image(output, b"new", force=True)
            self.assertEqual(output.read_bytes(), b"existing")
            self.assertEqual(list(output.parent.iterdir()), [output])

    def test_interrupted_fat_fallback_removes_only_partial_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "DestBin.bin"
            with (
                patch.object(child.os, "link", side_effect=OSError(errno.EPERM, "FAT")),
                patch.object(
                    child.os, "fsync", side_effect=[None, KeyboardInterrupt()]
                ),
                self.assertRaises(KeyboardInterrupt),
            ):
                child.save_image(output, b"new")
            self.assertEqual(list(output.parent.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
