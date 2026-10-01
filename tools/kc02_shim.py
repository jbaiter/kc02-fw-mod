#!/usr/bin/env python3
"""kc02_shim.py - hand-assembled RAM shim + canary for KC02 SPI-flash reads.

WHY THIS FILE EXISTS
--------------------
The KC02 stores its firmware in an SPI NOR flash chip that is **not** mapped
into the CPU address space (parent-confirmed: every flash access in the image
goes through the SPI controller 0x0203D38C; a linear window would collide with
the boot-time RAM zero-fill region).  The one data-exfiltration primitive the
stock firmware exposes over USB is the vendor ``0xCD`` BOT command, which calls
``0x0204C624`` and:
  * READ  (bmCBWFlags 0x80): optionally calls ``callback(r3=buffer, r4=length,
    r5=arg)``, then sends ``length`` bytes from ``buffer`` to the host.
  * WRITE (bmCBWFlags 0x00): receives bytes into ``buffer``, then calls
    ``callback(r3=buffer, r4=length, r5=arg)``.

The SPI reader we need is ``0x0203D38C(r3=flash_offset, r4=dest, r5=count)``.
The callback convention is ``(dest, count, offset)`` -- three values in the
wrong order -- so the host cannot point the callback slot at the SPI reader
directly.  This module hand-assembles the missing adapter: a 48-byte shim that
reshapes the arguments and tail-calls 0x0203D38C.  The firmware then sends the
buffer the shim just filled with flash bytes.

NO ASSEMBLER
------------
There is no working assembler for this core.  Every literal encoding below was
harvested from the stock image's own disassembly (see ``HARVESTED_ENCODINGS``)
and is re-checked by ``selftest()``.  Only ``l.jal`` is PC-relative and
therefore computed; its formula was derived from three independent firmware
call sites and re-verified in the self-test:

    target = inst_address + (sign_extend_26(word & 0x03FFFFFF) << 2)
    word   = 0x04000000 | (delta >> 2)          (delta = target - inst_address)

This is exactly the formula the project's own ``BootstrapOR1K.java`` uses to
seed l.jal targets, and it reproduces the harvested bytes of
``l.jal 0x0203d38c`` at 0x0205B2B8 (``3588ff07``) and ``l.jal 0x0204a698`` at
0x0204C500 (``66f8ff07``).

SAFETY
------
This file only *describes* code.  It never talks to a device and never touches
the firmware image or any Ghidra cache.  The shim calls exactly one firmware
routine (the SPI READ 0x0203D38C) and can never reach a flash-write/erase
routine.
"""

from __future__ import annotations

import argparse
import struct
import sys
from typing import Optional, Sequence

# --------------------------------------------------------------------------
# Load addresses (FIXED - the shim's l.jal is encoded for exactly these)
# --------------------------------------------------------------------------
# All three live in the proven-writable RAM window 0x02044000..0x027FEC00,
# above the "uncached low SRAM below 0x44000" guard inside cache_clean
# 0x0202B058, and clear of every RAM address this project has identified in
# use (0x020d1bd4/0x020d1e84/0x020d1ea0/0x020d2ef4/0x020d2efc,
# 0x020d416c..0x020d416e, 0x020d7420..0x020d7a20, 0x020da39c, 0x020da3ac,
# 0x020da3b0, 0x020da3d8, 0x020da40c, 0x020ce260, 0x020e32a0).
SHIM_LOAD_ADDR = 0x020D8000        # 48 bytes -> 0x020D802F
CANARY_LOAD_ADDR = 0x020D8080      # 4 bytes  -> 0x020D8083
DATA_BUFFER_ADDR = 0x020D8100      # 512 bytes -> 0x020D82FF (SPI chunk staging)

SHIM_LEN = 48
CANARY_LEN = 4
DATA_BUFFER_LEN = 512

# The only firmware routine the shim is allowed to call.
SPI_READ_FN = 0x0203D38C           # SPI_read(r3=flash_offset, r4=dest, r5=count)

# Scratch window reserved for these blobs.  Any host-side RAM write must land
# entirely inside it; see kc02_usb.py.
SCRATCH_WINDOW_LO = SHIM_LOAD_ADDR
SCRATCH_WINDOW_HI = DATA_BUFFER_ADDR + DATA_BUFFER_LEN   # exclusive

if SCRATCH_WINDOW_HI - SCRATCH_WINDOW_LO > 0x400:
    raise AssertionError("scratch window unexpectedly large")
if not (0x02044000 <= SCRATCH_WINDOW_LO < SCRATCH_WINDOW_HI <= 0x027FEC00):
    raise AssertionError("scratch window outside the proven-writable RAM range")
if SCRATCH_WINDOW_LO < 0x00040000:
    raise AssertionError("scratch window overlaps uncached low SRAM")


# --------------------------------------------------------------------------
# Encoders
# --------------------------------------------------------------------------
def enc_ori(rd: int, ra: int, imm16: int) -> bytes:
    """``b.ori rd,ra,imm16`` -> 4 bytes (little-endian).

    Layout recovered from the image: byte0/byte1 = imm16 LE, byte2 =
    (rd&7)<<5 | (ra&0x1F), byte3 = 0xA8 | ((rd>>3)&3).
    Cross-checked against ``b.ori r2,r4,0x0`` = 000044a8 (0x0204C4C4) and
    ``b.ori r20,r3,0x0`` = 000083aa (0x0204C4BC).
    """
    if not 0 <= rd <= 31 or not 0 <= ra <= 31:
        raise ValueError("register out of range")
    if not 0 <= imm16 <= 0xFFFF:
        raise ValueError("immediate out of 16-bit range")
    return bytes(
        (
            imm16 & 0xFF,
            (imm16 >> 8) & 0xFF,
            ((rd & 7) << 5) | (ra & 0x1F),
            0xA8 | ((rd >> 3) & 0x3),
        )
    )


def enc_jal(inst_addr: int, target: int) -> bytes:
    """``l.jal target`` placed at ``inst_addr`` -> 4 bytes (little-endian).

    Sets r9 = inst_addr + 4 and jumps.  See the module docstring for the
    formula and its provenance.
    """
    delta = target - inst_addr
    if delta % 4:
        raise ValueError(f"l.jal target 0x{target:08X} not word aligned")
    imm = delta >> 2
    if not -(1 << 25) <= imm < (1 << 25):
        raise ValueError(f"l.jal displacement {delta} out of 26-bit signed range")
    word = 0x04000000 | (imm & 0x03FFFFFF)
    return struct.pack("<I", word)


def enc_lj(inst_addr: int, target: int) -> bytes:
    """``l.j target`` placed at ``inst_addr`` -> 4 bytes (little-endian).

    Unlike ``l.jal`` this sets **no** link register, so r9 survives the jump.
    That is exactly what a jump-into-trampoline needs: the trampoline (and the
    patched function body it resumes) still sees the caller's return address in
    r9.  Word layout = ``(delta >> 2) & 0x03FFFFFF`` (opcode field 0x00).

    Provenance: stock ``l.j 0x02008290`` at 0x020082C0 is ``f4ffff03``;
    delta = 0x02008290 - 0x020082C0 = -0x30, (delta>>2) & 0x3FFFFFF =
    0x03FFFFF4.  Cross-checked against ``l.j 0x020081d4`` at 0x020082A0
    (``cdffff03``) and ``l.j 0x02000408`` targets used by the hoist stub.
    """
    delta = target - inst_addr
    if delta % 4:
        raise ValueError(f"l.j target 0x{target:08X} not word aligned")
    imm = delta >> 2
    if not -(1 << 25) <= imm < (1 << 25):
        raise ValueError(f"l.j displacement {delta} out of 26-bit signed range")
    return struct.pack("<I", imm & 0x03FFFFFF)


def mov(rd: int, ra: int) -> bytes:
    """``b.ori rd,ra,0`` == register move."""
    return enc_ori(rd, ra, 0)


# --------------------------------------------------------------------------
# Format-I / format-M encoders (the child-lock stubs need more than b.ori)
# --------------------------------------------------------------------------
# Layouts taken from the KC02 language definition that Ghidra analyses this
# image with (reference/ghidra-kc02-or1k/data/languages/OpenRISC_ORBIS32.sinc),
# and every one of them is cross-checked against real instructions harvested
# from the stock image (HARVESTED_ENCODINGS below + selftest).
#
#   format I : word = opcode<<26 | rD_or_cond<<21 | rA<<16 | imm16
#   format M : word = opcode<<26 | hi5(offset)<<21 | rA<<16 | rB<<11 | lo11(offset)
#              with offset = sign_extend5(hi5) << 11 | lo11   (16-bit signed)
#   branch   : word = opcode<<26 | ((target - inst_addr) >> 2)
#              relative to the BRANCH INSTRUCTION itself (this core has no
#              delay slots, so there is no +4 involved)

def _reg(name: str) -> int:
    if not name.startswith("r"):
        raise ValueError(f"not a register: {name!r}")
    num = int(name[1:])
    if not 0 <= num <= 31:
        raise ValueError(f"register out of range: {name!r}")
    return num


def _imm(text: str) -> int:
    return int(text, 0)


def _format_i(opcode: int, rd: int, ra: int, imm16: int) -> bytes:
    if not 0 <= rd <= 31 or not 0 <= ra <= 31:
        raise ValueError("register out of range")
    if not -0x8000 <= imm16 <= 0xFFFF:
        raise ValueError(f"immediate {imm16} out of 16-bit range")
    word = ((opcode & 0x3F) << 26) | ((rd & 0x1F) << 21) | ((ra & 0x1F) << 16)
    word |= imm16 & 0xFFFF
    return struct.pack("<I", word)


def _format_m(opcode: int, ra: int, rb: int, offset: int) -> bytes:
    """Store format: 16-bit signed offset split into a 5-bit high and 11-bit low part."""
    if not 0 <= ra <= 31 or not 0 <= rb <= 31:
        raise ValueError("register out of range")
    if not -0x8000 <= offset <= 0x7FFF:
        raise ValueError(f"offset {offset} out of 16-bit signed range")
    imm = offset & 0xFFFF
    word = ((opcode & 0x3F) << 26) | (((imm >> 11) & 0x1F) << 21)
    word |= ((ra & 0x1F) << 16) | ((rb & 0x1F) << 11) | (imm & 0x7FF)
    return struct.pack("<I", word)


# Flag conditions for the 0x2f ``b.sf<i>`` compare-with-immediate family.
COND_SFEQI = 0x0
COND_SFNEI = 0x1
COND_SFGTUI = 0x2
COND_SFLEUI = 0x5

OP_BRANCH_BNF = 0x03
OP_BRANCH_BF = 0x04
OP_MOVHI = 0x06
OP_LWZ = 0x21
OP_LBZ = 0x23
OP_ADDI = 0x27
OP_ANDI = 0x29
OP_ORI = 0x2A
OP_SFXXI = 0x2F
OP_SW = 0x35
OP_SB = 0x36


def enc_movhi(rd: int, imm16: int) -> bytes:
    """``b.movhi rd,imm16`` -> rd = imm16 << 16 (opcode 0x06, bit16 = 0)."""
    if not 0 <= imm16 <= 0xFFFF:
        raise ValueError("immediate out of 16-bit range")
    return _format_i(OP_MOVHI, rd, 0, imm16)


def enc_addi(rd: int, ra: int, imm16: int) -> bytes:
    """``b.addi rd,ra,imm16`` (opcode 0x27)."""
    return _format_i(OP_ADDI, rd, ra, imm16)


def enc_andi(rd: int, ra: int, imm16: int) -> bytes:
    """``b.andi rd,ra,imm16`` (opcode 0x29, unsigned immediate)."""
    if not 0 <= imm16 <= 0xFFFF:
        raise ValueError("immediate out of 16-bit range")
    return _format_i(OP_ANDI, rd, ra, imm16)


def enc_lbz(rd: int, ra: int, offset: int) -> bytes:
    """``b.lbz rd,offset(rA)`` - zero-extending byte load (opcode 0x23)."""
    return _format_i(OP_LBZ, rd, ra, offset)


def enc_sb(ra: int, rb: int, offset: int) -> bytes:
    """``b.sb offset(rA),rB`` - byte store (opcode 0x36, format M)."""
    return _format_m(OP_SB, ra, rb, offset)


def enc_sw(ra: int, rb: int, offset: int) -> bytes:
    """``b.sw offset(rA),rB`` - word store (opcode 0x35, format M)."""
    return _format_m(OP_SW, ra, rb, offset)


def enc_sfxi(cond: int, ra: int, imm16: int) -> bytes:
    """``b.sf<cond>i ra,imm16`` - set the SR flag from a compare (opcode 0x2f)."""
    if not 0 <= cond <= 0x1F:
        raise ValueError("condition out of range")
    return _format_i(OP_SFXXI, cond, ra, imm16)


def _enc_branch(opcode: int, inst_addr: int, target: int) -> bytes:
    delta = target - inst_addr
    if delta % 4:
        raise ValueError(f"branch target 0x{target:08X} not word aligned")
    imm = delta >> 2
    if not -(1 << 25) <= imm < (1 << 25):
        raise ValueError(f"branch displacement {delta} out of 26-bit signed range")
    return struct.pack("<I", ((opcode & 0x3F) << 26) | (imm & 0x03FFFFFF))


def enc_b_bnf(inst_addr: int, target: int) -> bytes:
    """``b.bnf target`` - branch if the SR flag is CLEAR (opcode 0x03)."""
    return _enc_branch(OP_BRANCH_BNF, inst_addr, target)


def enc_b_bf(inst_addr: int, target: int) -> bytes:
    """``b.bf target`` - branch if the SR flag is SET (opcode 0x04)."""
    return _enc_branch(OP_BRANCH_BF, inst_addr, target)


# mnemonic -> how to turn the disassembly text tail into encoder arguments.
def reencode_text(mnem: str, text: str, addr: int) -> bytes:
    """Re-assemble one disassembly line (``mnemonic text`` + its address).

    Used by the self-tests to prove the encoders against instructions harvested
    from the stock image, i.e. against bytes this project did not produce.
    """
    body = text.split(None, 1)[1].strip() if " " in text else ""
    if mnem in ("b.ori", "b.addi", "b.andi", "b.movhi"):
        parts = [p.strip() for p in body.split(",")]
        if mnem == "b.movhi":
            return enc_movhi(_reg(parts[0]), _imm(parts[1]))
        rd, ra, imm = _reg(parts[0]), _reg(parts[1]), _imm(parts[2])
        if mnem == "b.ori":
            return enc_ori(rd, ra, imm)
        if mnem == "b.addi":
            return enc_addi(rd, ra, imm)
        return enc_andi(rd, ra, imm)
    if mnem == "b.lbz":
        rd_s, mem = body.split(",")
        off_s, ra_s = mem.split("(")
        return enc_lbz(_reg(rd_s), _reg(ra_s.rstrip(")")), _imm(off_s))
    if mnem in ("b.sb", "b.sw"):
        mem, rb_s = body.split(",")
        off_s, ra_s = mem.split("(")
        args = (_reg(ra_s.rstrip(")")), _reg(rb_s), _imm(off_s))
        return enc_sb(*args) if mnem == "b.sb" else enc_sw(*args)
    if mnem in ("b.sfeqi", "b.sfnei", "b.sfgtui", "b.sfleui"):
        ra_s, imm_s = body.split(",")
        cond = {"b.sfeqi": COND_SFEQI, "b.sfnei": COND_SFNEI,
                "b.sfgtui": COND_SFGTUI, "b.sfleui": COND_SFLEUI}[mnem]
        return enc_sfxi(cond, _reg(ra_s), _imm(imm_s))
    if mnem in ("b.bf", "b.bnf"):
        target = _imm(body)
        return enc_b_bf(addr, target) if mnem == "b.bf" else enc_b_bnf(addr, target)
    if mnem == "l.j":
        return enc_lj(addr, _imm(body))
    if mnem == "l.jal":
        return enc_jal(addr, _imm(body))
    raise ValueError(f"no re-assembler for {mnem!r}")


# --------------------------------------------------------------------------
# Literal encodings harvested from the stock image (address = where copied from)
# --------------------------------------------------------------------------
ENC_ADD_SP_N8 = bytes.fromhex("f8ff219c")    # b.addi r1,r1,-0x8  @0x0202B068
ENC_ADD_SP_P8 = bytes.fromhex("0800219c")    # b.addi r1,r1,0x8   @0x0202B0C4
# Reference-only (harvested, not used by the current shim):
ENC_ADD_SP_N10 = bytes.fromhex("f0ff219c")   # b.addi r1,r1,-0x10 @0x0205B2A4
ENC_ADD_SP_P10 = bytes.fromhex("1000219c")   # b.addi r1,r1,0x10  @0x0205B368
ENC_SW_M8_R2 = bytes.fromhex("f817e1d7")     # b.sw -0x8(r1),r2   @0x0205B294
ENC_SW_M4_R9 = bytes.fromhex("fc4fe1d7")     # b.sw -0x4(r1),r9   @0x0205B29C
ENC_LWZ_M8_R2 = bytes.fromhex("f8ff4184")    # b.lwz r2,-0x8(r1)  @0x0205B374
ENC_LWZ_M4_R9 = bytes.fromhex("fcff2185")    # b.lwz r9,-0x4(r1)  @0x0205B36C
ENC_JR_R9 = bytes.fromhex("00480044")        # l.jr r9            @0x0205B378

# (addr, bytes, mnemonic, text) - used only to prove the encoders in selftest().
# Every entry is a REAL instruction read out of the stock image at that CPU
# address (file offset = CPU - 0x02000000 + 0x2600), so the self-test checks the
# encoders against bytes this project did not produce.
HARVESTED_ENCODINGS = (
    (0x0204C4C4, "000044a8", "b.ori", "b.ori r2,r4,0x0"),
    (0x0204C4BC, "000083aa", "b.ori", "b.ori r20,r3,0x0"),
    (0x0205B2AC, "0000a3a8", "b.ori", "b.ori r5,r3,0x0"),
    (0x0203D3B0, "000045a8", "b.ori", "b.ori r2,r5,0x0"),
    (0x0203D410, "000082a8", "b.ori", "b.ori r4,r2,0x0"),
    (0x0205B2B0, "000081a8", "b.ori", "b.ori r4,r1,0x0"),
    (0x0202B068, "f8ff219c", "b.addi", "b.addi r1,r1,-0x8"),
    (0x0203D430, "1800219c", "b.addi", "b.addi r1,r1,0x18"),
    (0x0205B2B8, "3588ff07", "l.jal", "l.jal 0x0203d38c"),
    (0x0204C500, "66f8ff07", "l.jal", "l.jal 0x0204a698"),
    (0x0203D3B8, "c8c0ff07", "l.jal", "l.jal 0x0202d6d8"),
    (0x0205B378, "00480044", "l.jr", "l.jr r9"),
    (0x020082C0, "f4ffff03", "l.j", "l.j 0x02008290"),
    (0x020082A0, "cdffff03", "l.j", "l.j 0x020081d4"),
    (0x0204C580, "00300048", "l.jalr", "l.jalr r6"),
    (0x0204C52C, "f817e1d7", "b.sw", "b.sw -0x8(r1),r2"),
    (0x0205B29C, "fc4fe1d7", "b.sw", "b.sw -0x4(r1),r9"),
    (0x0205B374, "f8ff4184", "b.lwz", "b.lwz r2,-0x8(r1)"),
    (0x0205B36C, "fcff2185", "b.lwz", "b.lwz r9,-0x4(r1)"),
    # --- added for the child-lock stubs (all read from the stock image) -----
    (0x020004B8, "0c026018", "b.movhi", "b.movhi r3,0x20c"),
    (0x020004A0, "0a00609c", "b.addi", "b.addi r3,r0,0xa"),
    (0x0200994C, "0300609c", "b.addi", "b.addi r3,r0,0x3"),
    (0x02007A10, "ff0063a4", "b.andi", "b.andi r3,r3,0xff"),
    (0x02009F3C, "6800838c", "b.lbz", "b.lbz r4,0x68(r3)"),
    (0x020098CC, "6d004e8c", "b.lbz", "b.lbz r2,0x6d(r14)"),
    (0x02009F4C, "692003d8", "b.sb", "b.sb 0x69(r3),r4"),
    (0x020098F4, "6da00ed8", "b.sb", "b.sb 0x6d(r14),r20"),
    (0x02009890, "f497e1d7", "b.sw", "b.sw -0xc(r1),r18"),
    (0x02009F14, "010004bc", "b.sfeqi", "b.sfeqi r4,0x1"),
    (0x02009F24, "000024bc", "b.sfnei", "b.sfnei r4,0x0"),
    (0x020098BC, "020034bc", "b.sfnei", "b.sfnei r20,0x2"),
    (0x02007A04, "1300a4bc", "b.sfleui", "b.sfleui r4,0x13"),
    (0x020079EC, "2d0042bc", "b.sfgtui", "b.sfgtui r2,0x2d"),
    (0x02009F1C, "0e00000c", "b.bnf", "b.bnf 0x02009f54"),
    (0x020098B4, "ba000010", "b.bf", "b.bf 0x02009b9c"),
    (0x0200049C, "04000010", "b.bf", "b.bf 0x020004ac"),
)


# --------------------------------------------------------------------------
# The shim itself
# --------------------------------------------------------------------------
def _shim_instructions(base: int) -> "list[tuple[bytes, str, str]]":
    """Return (bytes, mnemonic, text) for the shim assembled for load address base.

    The prologue and epilogue follow the firmware's own idiom exactly (compare
    0x0205B294 and 0x0203D430): stores and loads use negative offsets from the
    *entry* r1, and the epilogue restores r1 before loading:

        b.sw -0x8(r1),r2 / b.sw -0x4(r1),r9   stores land below the caller frame
        b.addi r1,r1,-0x8                      ... which now spans them
        <body>
        b.addi r1,r1,0x8                       r1 back to the entry value
        b.lwz r2,-0x8(r1) / b.lwz r9,-0x4(r1)  same addresses as the stores
        l.jr r9

    The shim is entered by `l.jalr` from 0x0204C580 with r1 already pointing at
    the bottom of 0x0204C52C's frame, so the eight bytes it claims sit in fresh
    stack space and the caller's saved r2/r9/r1 at -0x8/-0x4/-0xc are untouched.
    """
    return [
        # --- prologue: save r2 and r9, claim 8 bytes of stack --------------
        (ENC_SW_M8_R2, "b.sw", "b.sw -0x8(r1),r2"),
        (ENC_SW_M4_R9, "b.sw", "b.sw -0x4(r1),r9"),
        (ENC_ADD_SP_N8, "b.addi", "b.addi r1,r1,-0x8"),
        # --- reshape (dest, count, offset) -> (offset, dest, count) --------
        (mov(2, 3), "b.ori", "b.ori r2,r3,0x0"),      # r2 = dest
        (mov(3, 5), "b.ori", "b.ori r3,r5,0x0"),      # r3 = flash offset
        (mov(5, 4), "b.ori", "b.ori r5,r4,0x0"),      # r5 = count
        (mov(4, 2), "b.ori", "b.ori r4,r2,0x0"),      # r4 = dest
        # --- SPI_read(offset, dest, count) ---------------------------------
        (enc_jal(base + 7 * 4, SPI_READ_FN), "l.jal", "l.jal 0x0203d38c"),
        # --- epilogue: release the frame, restore r2/r9, return ------------
        (ENC_ADD_SP_P8, "b.addi", "b.addi r1,r1,0x8"),
        (ENC_LWZ_M8_R2, "b.lwz", "b.lwz r2,-0x8(r1)"),
        (ENC_LWZ_M4_R9, "b.lwz", "b.lwz r9,-0x4(r1)"),
        (ENC_JR_R9, "l.jr", "l.jr r9"),
    ]


def build_shim(base: int = SHIM_LOAD_ADDR) -> bytes:
    """Assemble the shim for a fixed load address."""
    if base % 4:
        raise ValueError("shim load address must be word aligned")
    return b"".join(b for b, _m, _t in _shim_instructions(base))


def expected_shim_disassembly(base: int = SHIM_LOAD_ADDR) -> "list[tuple[int, str, str, bytes]]":
    """(address, mnemonic, text, bytes) that a correct disassembly must show."""
    out = []
    addr = base
    for raw, mnem, text in _shim_instructions(base):
        out.append((addr, mnem, text, raw))
        addr += 4
    return out


SHIM_BYTES = build_shim(SHIM_LOAD_ADDR)
CANARY_BYTES = ENC_JR_R9                       # single instruction: l.jr r9

if len(SHIM_BYTES) != SHIM_LEN:
    raise AssertionError(f"shim is {len(SHIM_BYTES)} bytes, expected {SHIM_LEN}")
if len(CANARY_BYTES) != CANARY_LEN:
    raise AssertionError("canary length mismatch")
if SHIM_LOAD_ADDR + SHIM_LEN > CANARY_LOAD_ADDR:
    raise AssertionError("shim overlaps the canary slot")
if CANARY_LOAD_ADDR + CANARY_LEN > DATA_BUFFER_ADDR:
    raise AssertionError("canary overlaps the data buffer")
if DATA_BUFFER_LEN % 16:
    raise AssertionError("data buffer must be a multiple of 16 (SPI DMA round-up)")


def shim_hex() -> str:
    """Space-separated hex of the shim bytes, as a hard-coded blob in the tool."""
    return " ".join(f"{b:02x}" for b in SHIM_BYTES)


# --------------------------------------------------------------------------
# Offline self-tests
# --------------------------------------------------------------------------
def selftest() -> int:
    """Verify the encoders against harvested firmware encodings and the shim layout."""
    failures = []

    def check(cond: bool, msg: str) -> None:
        if not cond:
            failures.append(msg)

    # 1. b.ori encoder reproduces every harvested b.ori encoding.
    check(enc_ori(2, 3, 0x0).hex() == "000043a8", "mov r2,r3 encoding")
    check(enc_ori(3, 5, 0x0).hex() == "000065a8", "mov r3,r5 encoding")
    check(enc_ori(5, 4, 0x0).hex() == "0000a4a8", "mov r5,r4 encoding")
    check(mov(4, 2).hex() == "000082a8", "mov r4,r2 encoding")
    for _addr, hx, mnem, _text in HARVESTED_ENCODINGS:
        if mnem != "b.ori":
            continue
        # text form: "b.ori rD,rA,0xIMM"
        body = _text.split()[1]
        rd_s, ra_s, imm_s = body.split(",")
        got = enc_ori(int(rd_s[1:]), int(ra_s[1:]), int(imm_s, 16)).hex()
        check(got == hx, f"b.ori encoder mismatch at 0x{_addr:08X}: {got} != {hx}")

    # 2. l.jal encoder reproduces every harvested l.jal encoding.
    for addr, hx, mnem, text in HARVESTED_ENCODINGS:
        if mnem != "l.jal":
            continue
        target = int(text.split()[1], 16)
        got = enc_jal(addr, target).hex()
        check(got == hx, f"l.jal encoder mismatch at 0x{addr:08X}: {got} != {hx}")

    # 3. l.jal is self-inverse in the documented sense.
    check(enc_jal(SPI_READ_FN, SPI_READ_FN).hex() == "00000004", "l.jal +0 self")

    # 3b. l.j encoder reproduces every harvested l.j encoding.
    for addr, hx, mnem, text in HARVESTED_ENCODINGS:
        if mnem != "l.j":
            continue
        target = int(text.split()[1], 16)
        got = enc_lj(addr, target).hex()
        check(got == hx, f"l.j encoder mismatch at 0x{addr:08X}: {got} != {hx}")
    check(enc_lj(SPI_READ_FN, SPI_READ_FN).hex() == "00000000", "l.j +0 self")

    # 3c. every encoder in reencode_text() reproduces its harvested encoding.
    for addr, hx, mnem, text in HARVESTED_ENCODINGS:
        if mnem not in ("b.ori", "b.addi", "b.andi", "b.movhi", "b.lbz",
                        "b.sb", "b.sw", "b.sfeqi", "b.sfnei", "b.sfgtui",
                        "b.sfleui", "b.bf", "b.bnf", "l.j", "l.jal"):
            continue
        got = reencode_text(mnem, text, addr).hex()
        check(got == hx, f"{mnem} encoder mismatch at 0x{addr:08X}: {got} != {hx}")
    # the flag-setting compare + branch polarity pair used by the child-lock gate
    check(enc_b_bnf(0x02009F1C, 0x02009F54).hex() == "0e00000c", "b.bnf polarity")
    check(enc_b_bf(0x0200049C, 0x020004AC).hex() == "04000010", "b.bf polarity")
    # format M round-trip: the harvested stores decode as the offset splits
    check(enc_sw(1, 9, -4).hex() == "fc4fe1d7", "b.sw -0x4(r1),r9")
    check(enc_sb(14, 20, 0x6D).hex() == "6da00ed8", "b.sb 0x6d(r14),r20")
    check(enc_sb(3, 4, 0x69).hex() == "692003d8", "b.sb 0x69(r3),r4")
    # ... and a negative offset splits the same way b.sw does
    check(enc_sb(1, 2, -1).hex()[0:2] == "ff", "negative store offset sign-extends")

    # 4. Shim layout: 12 word instructions at the documented addresses.
    check(len(SHIM_BYTES) == 48, "shim length")
    exp = expected_shim_disassembly()
    jal = [e for e in exp if e[1] == "l.jal"]
    check(len(jal) == 1, "exactly one l.jal in the shim")
    check(jal and jal[0][0] == SHIM_LOAD_ADDR + 0x1C, "l.jal placed at shim+0x1C")
    check(SHIM_BYTES[0x1C:0x20].hex() == "dc94fd07",
          "l.jal 0x0203d38c from 0x020d801c encodes as dc94fd07")
    check(enc_jal(SHIM_LOAD_ADDR + 0x1C, SPI_READ_FN).hex() == "dc94fd07",
          "l.jal 0x0203d38c from 0x020d801c encodes as dc94fd07")
    check(SHIM_BYTES[0x2C:0x30] == ENC_JR_R9, "shim ends with l.jr r9")
    check(CANARY_BYTES == ENC_JR_R9, "canary is l.jr r9")

    # 5. Argument reshuffle and stack idiom appear in the right order.
    words = [SHIM_BYTES[i:i + 4].hex() for i in range(0, len(SHIM_BYTES), 4)]
    check(words[0] == "f817e1d7", "shim[0] = b.sw -0x8(r1),r2")
    check(words[1] == "fc4fe1d7", "shim[1] = b.sw -0x4(r1),r9")
    check(words[2] == "f8ff219c", "shim[2] = b.addi r1,r1,-0x8")
    check(words[3] == "000043a8", "shim[3] = mov r2,r3")
    check(words[4] == "000065a8", "shim[4] = mov r3,r5")
    check(words[5] == "0000a4a8", "shim[5] = mov r5,r4")
    check(words[6] == "000082a8", "shim[6] = mov r4,r2")
    check(words[8] == "0800219c", "shim[8] = b.addi r1,r1,0x8")
    check(words[9] == "f8ff4184", "shim[9] = b.lwz r2,-0x8(r1)")
    check(words[10] == "fcff2185", "shim[10] = b.lwz r9,-0x4(r1)")
    check(words[11] == "00480044", "shim[11] = l.jr r9")
    # The epilogue must restore r1 BEFORE the loads, so the load offsets match
    # the store offsets (this is the firmware's own idiom).
    check(words[0] == "f817e1d7" and words[9] == "f8ff4184",
          "r2 is spilled and reloaded at the same -0x8(r1)")
    check(words[1] == "fc4fe1d7" and words[10] == "fcff2185",
          "r9 is spilled and reloaded at the same -0x4(r1)")
    check(words[2] == "f8ff219c" and words[8] == "0800219c",
          "stack adjust is balanced (-0x8 then +0x8)")

    # 6. A different load address must produce a different l.jal (no accidental
    #    hard-coding) while keeping every other instruction identical.
    other = build_shim(SHIM_LOAD_ADDR + 0x1000)
    check(len(other) == 48, "relocated shim length")
    check(other[:0x1C] == SHIM_BYTES[:0x1C], "relocated shim prologue unchanged")
    check(other[0x20:] == SHIM_BYTES[0x20:], "relocated shim epilogue unchanged")
    check(other[0x1C:0x20] != SHIM_BYTES[0x1C:0x20], "l.jal is position dependent")

    if failures:
        print("SELFTEST FAILED:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    print(f"selftest OK: shim {len(SHIM_BYTES)} bytes @0x{SHIM_LOAD_ADDR:08X}, "
          f"canary {len(CANARY_BYTES)} bytes @0x{CANARY_LOAD_ADDR:08X}, "
          f"data {DATA_BUFFER_LEN} bytes @0x{DATA_BUFFER_ADDR:08X}")
    print(f"shim bytes: {shim_hex()}")
    return 0


def expected_canary_disassembly(base: int = CANARY_LOAD_ADDR) -> "list[tuple[int, str, str, bytes]]":
    """(address, mnemonic, text, bytes) for the 1-instruction canary."""
    return [(base, "l.jr", "l.jr r9", ENC_JR_R9)]


def expected_disassembly(kind: str) -> "list[tuple[int, str, str, bytes]]":
    if kind == "shim":
        return expected_shim_disassembly()
    if kind == "canary":
        return expected_canary_disassembly()
    raise ValueError(f"unknown blob kind: {kind!r}")


def verify_roundtrip(path: str, kind: str = "shim") -> int:
    """Check a Ghidra disassembly dump of a blob against the assembled bytes.

    Accepts TSV lines: ``0xADDR<TAB>mnemonic<TAB>text<TAB>hexbytes``.
    """
    expected = expected_disassembly(kind)
    lines = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            raw = raw.rstrip("\n")
            if not raw or raw.startswith("#"):
                continue
            lines.append(raw)

    failures = []
    if len(lines) < len(expected):
        failures.append(f"disassembly has {len(lines)} instructions, expected {len(expected)}")

    for idx, (addr, mnem, text, raw) in enumerate(expected):
        if idx >= len(lines):
            break
        parts = lines[idx].split("\t")
        if parts[0].strip().lower().startswith("missing"):
            failures.append(f"instruction at 0x{addr:08X} was not disassembled")
            continue
        if len(parts) < 4:
            failures.append(f"malformed line {idx + 1}: {lines[idx]!r}")
            continue
        d_addr = int(parts[0], 16)
        d_mnem, d_text, d_bytes = parts[1].strip(), parts[2].strip(), parts[3].strip().lower()
        if d_addr != addr:
            failures.append(f"#{idx}: address 0x{d_addr:08X} != 0x{addr:08X}")
        if d_mnem != mnem:
            failures.append(f"#{idx} 0x{addr:08X}: mnemonic {d_mnem!r} != {mnem!r}")
        if d_text != text:
            failures.append(f"#{idx} 0x{addr:08X}: text {d_text!r} != {text!r}")
        if d_bytes != raw.hex():
            failures.append(
                f"#{idx} 0x{addr:08X}: bytes {d_bytes} != assembled {raw.hex()}"
            )

    if failures:
        print(f"ROUND-TRIP FAILED ({kind}):", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    print(f"round-trip OK ({kind}): {len(expected)} instruction(s) match "
          f"byte-for-byte and decode to the intended mnemonic+operands:")
    for addr, mnem, text, raw in expected:
        print(f"  0x{addr:08X}  {raw.hex()}  {text}")
    return 0


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def _cmd_selftest(_args: argparse.Namespace) -> int:
    return selftest()


def _cmd_blob(args: argparse.Namespace) -> int:
    blob = SHIM_BYTES if args.kind == "shim" else CANARY_BYTES
    addr = SHIM_LOAD_ADDR if args.kind == "shim" else CANARY_LOAD_ADDR
    if args.out:
        with open(args.out, "wb") as fh:
            fh.write(blob)
        print(f"wrote {len(blob)} bytes of {args.kind} (load address "
              f"0x{addr:08X}) -> {args.out}")
    else:
        print(f"{args.kind}: load address 0x{addr:08X}, {len(blob)} bytes")
        print(" ".join(f"{b:02x}" for b in blob))
    return 0


def _cmd_verify(args: argparse.Namespace) -> int:
    return verify_roundtrip(args.path, args.kind)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="kc02_shim.py",
        description="Hand-assembled KC02 RAM shim + canary for SPI flash reads "
                    "(offline assembler and verifier; never touches a device).",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_self = sub.add_parser("selftest", help="check encoders and shim layout offline")
    p_self.set_defaults(func=_cmd_selftest)

    p_blob = sub.add_parser("blob", help="emit the raw shim/canary bytes")
    p_blob.add_argument("--kind", choices=("shim", "canary"), default="shim")
    p_blob.add_argument("--out", default=None, help="write raw bytes to this file")
    p_blob.set_defaults(func=_cmd_blob)

    p_ver = sub.add_parser("verify-roundtrip",
                           help="check a Ghidra disassembly dump (TSV) of a blob")
    p_ver.add_argument("path")
    p_ver.add_argument("--kind", choices=("shim", "canary"), default="shim")
    p_ver.set_defaults(func=_cmd_verify)

    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
