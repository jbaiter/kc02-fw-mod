#!/usr/bin/env python3
"""Build/restore an experimental persistent KC02 child-mode SD-update image.

Boot into camera mode; gate its POWER/MENU handler with a one-shot UP arm.
Keep stock UP behaviour and USB mode. No USB access or installation occurs here.

The entire replacement fits the original 100-byte POWER handler: a leaf gate,
UP trampoline, and a four-byte control word. The application loader copies that
word to RAM on boot; runtime writes change RAM, not flash. No code cave, asset
space, heap allocation, or 0x020D9xxx scratch is needed.

Only the researched boot/application hashes are accepted (assets and the tail
may differ). OpenRISC-compatible LE32, no delay slots. Both handlers are primary
key callbacks: the guarded stock dispatcher sets r4=1 and r9=0x02007A9C. We use
that known link address as the base of the control-word loads/stores. The UP
trampoline preserves its displaced stack save and returns to the original body.
The leaf POWER handler updates the stock camera state before tail-calling the
unchanged ui_mode_request, which doesn't access those state bytes and leaves
r11 unchanged. It preserves the stock flag/payload checks and zero return value.

Hardware validation of this persistent layout is still pending. An invalid code
patch can prevent SD recovery; keep a full backup and an external SPI programmer.
"""

from __future__ import annotations

import argparse
import errno
import hashlib
import os
import struct
import sys
import tempfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
FLASH_SIZE = 0x400000
APP_START, APP_END = 0x2600, 0xD3200
BOOT_SHA256 = "eed1dddb1023e1bd8a9fbcf51b86e315bf0e492928a2a86c5b396b830e57e563"
APP_SHA256 = "9d92f3e502da9d0376b8d6d4b0c8a09b1c5f2fba8344b992fd31285289ef11f4"
POWER = 0x02009F08
UP = 0x02009890
PRIMARY_RETURN = 0x02007A9C
MODE_REQUEST = 0x02008108
ARM = POWER + 21 * 4
CONTROL = POWER + 24 * 4
CONTROL_OFFSET = CONTROL - PRIMARY_RETURN
STOCK_POWER = bytes.fromhex(
    "fc4fe1d7f40fe1d7f817e1d7010004bcf4ff219c0e00000c00008584"
    "000024bc0b0000100600609c76f8ff070d0260186c4163a86800838c"
    "000024bc040000100200409c692003d8681003d80c00219c0000609d"
    "fcff2185f4ff2184f8ff418400480044"
)


class PatchError(Exception):
    """Refuse an unsupported image or an unsafe output operation."""


def file_offset(address):
    return address - 0x02000000 + APP_START


def word(opcode, rd=0, ra=0, immediate=0):
    """Encode the register/immediate form of the small OR1K subset below."""
    return (opcode << 26) | (rd << 21) | (ra << 16) | (immediate & 0xFFFF)


def store_byte(ra, rb, immediate):
    # Store immediates split across bits 25..21 and 10..0, unlike loads.
    return (
        (0x36 << 26)
        | ((immediate & 0xF800) << 10)
        | (ra << 16)
        | (rb << 11)
        | (immediate & 0x7FF)
    )


def branch(opcode, pc, target):
    delta = target - pc
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise PatchError("Unaligned or out-of-range branch target")
    return (opcode << 26) | ((delta // 4) & 0x03FFFFFF)


def power_listing():
    """Return (instruction word, assembly) pairs; final word is RAM state.

    CONTROL byte 0 is the one-shot arm, byte 1 a runtime adult override.
    Loading both as a halfword admits either, but consuming only byte 0 keeps
    the override intact. Both start at zero after the application loads.
    """
    deny, request = POWER + 19 * 4, POWER + 16 * 4
    c = CONTROL_OFFSET
    return [
        (word(0x2F, 0, 4, 1), "l.sfeqi r4, 1"),
        (branch(0x03, POWER + 4, deny), f"l.bnf 0x{deny:08X}"),
        (word(0x21, 4, 5), "l.lwz r4, 0(r5)"),
        (word(0x2F, 1, 4), "l.sfnei r4, 0"),
        (branch(0x04, POWER + 16, deny), f"l.bf 0x{deny:08X}"),
        (word(0x25, 6, 9, c), f"l.lhz r6, 0x{c:X}(r9)  # arm | adult<<8"),
        (word(0x2F, 0, 6), "l.sfeqi r6, 0"),
        (branch(0x04, POWER + 28, deny), f"l.bf 0x{deny:08X}"),
        (store_byte(9, 0, c), f"l.sb 0x{c:X}(r9), r0  # consume arm only"),
        (word(0x06, 6, 0, 0x020D), "l.movhi r6, 0x020D"),
        (word(0x23, 7, 6, 0x41D4), "l.lbz r7, 0x41D4(r6)"),
        (word(0x2F, 1, 7), "l.sfnei r7, 0"),
        (branch(0x04, POWER + 48, request), f"l.bf 0x{request:08X}"),
        (store_byte(6, 0, 0x41D5), "l.sb 0x41D5(r6), r0  # stock state update"),
        (word(0x27, 7, 0, 2), "l.addi r7, r0, 2"),
        (store_byte(6, 7, 0x41D4), "l.sb 0x41D4(r6), r7"),
        (word(0x27, 3, 0, 6), "l.addi r3, r0, 6  # Main menu"),
        (word(0x27, 11), "l.addi r11, r0, 0  # stock handler return value"),
        (
            branch(0x00, POWER + 72, MODE_REQUEST),
            f"l.j 0x{MODE_REQUEST:08X}  # tail call; keep r9",
        ),
        (word(0x27, 11), "l.addi r11, r0, 0"),
        (0x44004800, "l.jr r9  # deny: no stack changes"),
        (store_byte(9, 4, c), f"l.sb 0x{c:X}(r9), r4  # UP: arm=1"),
        (0xD7E197F4, "l.sw -12(r1), r18  # displaced stock UP instruction"),
        (branch(0x00, ARM + 8, UP + 4), f"l.j 0x{UP + 4:08X}  # resume stock UP"),
        (0, ".word 0  # initial arm/adult bytes; data, not executable"),
    ]


def patch_sites():
    """The complete, fixed file-write allowlist (108 bytes in three spans)."""
    body = b"".join(
        struct.pack("<I", instruction) for instruction, _ in power_listing()
    )
    if len(body) != len(STOCK_POWER) or len(body) != 100:
        raise PatchError("Replacement escaped the POWER-handler footprint")
    return (
        ("boot mode", 0x2AAC, bytes.fromhex("0600609c"), bytes.fromhex("0300609c")),
        (
            "UP trampoline",
            file_offset(UP),
            bytes.fromhex("f497e1d7"),
            struct.pack("<I", branch(0, UP, ARM)),
        ),
        ("POWER gate / UP arm / state", file_offset(POWER), STOCK_POWER, body),
    )


def validate_header(image):
    if len(image) != FLASH_SIZE:
        raise PatchError("Expected a complete 4 MiB firmware image")
    if (
        image[4:8] != b"BLDR"
        or image[0x1FE:0x200] != b"\x55\xaa"
        or sum(image[:512]) % 256
        or image[0x304:0x308] != b"SFAT"
    ):
        raise PatchError("Invalid BLDR/header checksum/boot signature/SFAT marker")


def patch_image(image, restore=False, boot_only=False):
    """Accept stock or this exact patch, normalize, hash-check, then transform.

    Assets outside the loaded application may differ; unrecognized code,
    initialized data, bootloader/configuration, and partial patches are refused.
    A deliberate boot-only rehearsal is recognized as a separate valid stage.
    Restore undoes only our three spans, preserving the caller's theme/tail.
    """
    if restore and boot_only:
        raise PatchError("Restore and boot-only are mutually exclusive")
    validate_header(image)
    normalized = bytearray(image)
    states = []
    for name, offset, stock, patched in patch_sites():
        actual = image[offset : offset + len(stock)]
        if actual == stock:
            states.append("stock")
        elif actual == patched:
            states.append("patched")
        else:
            raise PatchError(f"Unexpected bytes at {name} (file 0x{offset:06X})")
        normalized[offset : offset + len(stock)] = stock
    supported_states = {
        ("stock", "stock", "stock"),
        ("patched", "stock", "stock"),  # deliberate boot-only rehearsal
        ("patched", "patched", "patched"),
    }
    if tuple(states) not in supported_states:
        raise PatchError("Partial/mixed child-mode patch; refusing to guess")
    if hashlib.sha256(normalized[:APP_START]).hexdigest() != BOOT_SHA256:
        raise PatchError("Unsupported bootloader/configuration layout")
    if hashlib.sha256(normalized[APP_START:APP_END]).hexdigest() != APP_SHA256:
        raise PatchError(
            "Unsupported application: code/initialized data differ from the researched image"
        )
    result = bytearray(image)
    for i, (_, offset, stock, patched) in enumerate(patch_sites()):
        use_stock = restore or (boot_only and i != 0)
        result[offset : offset + len(stock)] = stock if use_stock else patched
    validate_header(result)
    return bytes(result)


def save_image(output, data, force=False):
    """Stage beside the destination; protect existing files unless forced."""
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=output.parent, prefix=".child-mode-", delete=False
        ) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if force:
            os.replace(temporary, output)
        else:
            try:
                os.link(temporary, output)
            except OSError as exc:
                if exc.errno not in (
                    errno.EPERM,
                    errno.EOPNOTSUPP,
                    errno.ENOSYS,
                    errno.EXDEV,
                ):
                    raise
                # FAT/exFAT may lack hard links. Exclusive creation still keeps
                # existing files safe; delete only an incomplete file we created.
                with output.open("xb") as stream:
                    try:
                        stream.write(data)
                        stream.flush()
                        os.fsync(stream.fileno())
                    except BaseException:
                        stream.close()
                        output.unlink()
                        raise
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument(
        "-i",
        "--input",
        type=Path,
        default=PROJECT / "firmware/original.bin",
        help="4 MiB dump or asset-only themed image",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="output image (default: patched/child_mode/[stage/]DestBin.bin)",
    )
    stages = parser.add_mutually_exclusive_group()
    stages.add_argument(
        "--restore",
        action="store_true",
        help="remove this exact patch, retaining assets",
    )
    stages.add_argument(
        "--boot-only",
        action="store_true",
        help="first-stage rehearsal: boot into camera, leave both key handlers stock",
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="validate and report without writing"
    )
    parser.add_argument(
        "--force", action="store_true", help="replace output, never the input image"
    )
    parser.add_argument(
        "--listing", action="store_true", help="print generated assembly and exit"
    )
    args = parser.parse_args(argv)
    if args.listing:
        for i, (instruction, assembly) in enumerate(power_listing()):
            print(
                f"{POWER + i * 4:08X}  {struct.pack('<I', instruction).hex(' '):11}  {assembly}"
            )
        return 0
    if args.output is None:
        directory = PROJECT / "patched/child_mode"
        if args.restore:
            directory /= "restore"
        elif args.boot_only:
            directory /= "boot_only"
        args.output = directory / "DestBin.bin"
    try:
        if args.input.resolve() == args.output.resolve():
            raise PatchError(
                "Input and output must be different; preserve your source backup"
            )
        original = args.input.read_bytes()
        result = patch_image(original, args.restore, args.boot_only)
        for name, offset, stock, _ in patch_sites():
            before = original[offset : offset + len(stock)]
            after = result[offset : offset + len(stock)]
            changes = sum(a != b for a, b in zip(before, after))
            print(f"  0x{offset:06X} +{len(stock):3}: {name} ({changes} changed bytes)")
        print(f"Input SHA-256:  {hashlib.sha256(original).hexdigest()}")
        print(f"Output SHA-256: {hashlib.sha256(result).hexdigest()}")
        print(f"Changed bytes: {sum(a != b for a, b in zip(original, result))}")
        print(
            "⚠️ Experimental code patch; persistent layout has not been hardware-tested."
        )
        if args.dry_run:
            print("✅ Image validated; dry run, no files written.")
        else:
            save_image(args.output, result, args.force)
            print(f"✅ Saved {args.output}; no USB commands sent.")
        return 0
    except (OSError, PatchError) as exc:
        print(f"❌ {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("❌ Interrupted; no installation performed.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
