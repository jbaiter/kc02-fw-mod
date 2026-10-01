#!/usr/bin/env python3
"""kc02_usb.py - USB memory/flash reader for the HiMont KC02 camera.

SAFETY INVARIANT (read this before changing anything)
-----------------------------------------------------
This tool talks to the user's own KC02 over the *stock* vendor USB interface.
It can never write or erase flash. Every device transaction goes through one
choke point, ``_build_cbw()``, which enforces four hard rules:

  1. The function-pointer slot (CDB[1..4]) may only ever be the stock memory
     transfer routine ``0x0204C624``.  A bad pointer that lands on a sector
     erase or page program is *the* brick vector, so it is impossible by
     construction.
  2. The callback slot (CDB[9..12]) may only ever be ``0xFFFFFFFF`` (skip),
     ``0x0202B058`` (D-cache clean helper) or one of the two tiny RAM stubs
     this tool loads (the SPI-read shim / the canary).
  3. A host->device (write) transaction is only possible into the designated
     RAM scratch window (see ``WRITABLE_WINDOWS``).  Any address outside it
     raises.  There is no arbitrary-address write and no arbitrary call.
  4. The direction byte (bmCBWFlags) is re-derived from the caller's intent
     and validated; a write can never be issued to a non-scratch address.

The following flash-modifying routines must stay unreachable over this
transport and are listed in FORBIDDEN_FUNCTIONS:

    * 0x0203D584              4 KB SECTOR ERASE
    * 0x0203D454 / 0x0203D4E8  PAGE PROGRAM (writes flash)
    * 0x0203D25C              WREN (write-enable latch)
    * 0x02002418              DestBin.bin upgrade task (erase + program)

Flash contents are read with the SPI reader 0x0203D38C, which this tool can
only reach through the RAM shim (see tools/kc02_shim.py).

Hardware / transport facts (verified elsewhere in this project)
--------------------------------------------------------------
Mode 0 enumerates as VID:PID 0x0219:0x3280, USB mass storage (Bulk-Only
Transport), with exactly one interface: bInterfaceNumber 4, class/subclass/
protocol 08/06/50, bulk endpoints 0x81 IN and 0x01 OUT, wMaxPacketSize 512.
Mode 2 (0x1908:0x3283) exposes the same interface 4 bulk pair alongside UVC.
On Linux the usb-storage kernel driver claims interface 4, so we call
dev.set_auto_detach_kernel_driver(True) before set_configuration() and
claim_interface(4).

The vendor 0xCD BOT command calls the pointer in CDB[1..4] with the callback
in CDB[9..12] and the 24-bit argument in CDB[13..15].  Pointed at the stock
memory transfer routine 0x0204C624 with bmCBWFlags 0x80, the firmware
optionally calls callback(r3=CDB[5..8] buffer, r4=CBWDataTransferLength,
r5=CDB[13..15] arg) and then sends CBWDataTransferLength bytes from that
buffer.  With bmCBWFlags 0x00 the firmware instead receives those bytes into
the buffer and then calls the callback.  A buffer field of 0xFFFFFFFF selects
the firmware's fixed 512-byte USB scratch instead.

Requirements: Python 3, pyusb (`pip install pyusb`) and a working libusb
backend (Linux: package `libusb-1.0-0`; add a udev rule or run with sufficient
privileges to claim the interface).

Usage:
    python3 tools/kc02_usb.py selftest        # offline, no device
    python3 tools/kc02_usb.py shim-info       # offline, no device
    python3 tools/kc02_usb.py probe
    python3 tools/kc02_usb.py read --addr 0x01FFDA04 --len 0x10
    python3 tools/kc02_usb.py read --addr 0x020C869C --len 0x1000 --out chunk.bin
    python3 tools/kc02_usb.py dump-ram --out ram.bin
    python3 tools/kc02_usb.py load-shim      # RAM scratch write only
    python3 tools/kc02_usb.py canary         # RAM-executability probe
    python3 tools/kc02_usb.py flash-read --offset 0x0 --len 0x400000 --out flash.bin
    python3 tools/kc02_usb.py poke --addr 0x020CCD50 --hex "06 00 00 00"
    python3 tools/kc02_usb.py unlock-usb          # RAM-only USB lock-screen patch
    python3 tools/kc02_usb.py unlock-usb --rec stock_rec.bin   # offline dry run

This tool never writes or erases flash.  Its only writes go to declared RAM
windows, they are hard-bounded to them, and every write is read straight back
and verified before it is trusted.  ``poke``/``unlock-usb`` may write anywhere
in writable app RAM (0x020c0000..0x027FEC00) and nowhere else: flash, the SPI
controller and every MMIO/SPR address (0x8040/0x8250 USB scratch, 0x8900/0x8910
LCDC, 0x20000002/0x20000003 device registers, ...) are outside the window and
raise.  The 0xCD function-pointer slot stays pinned to 0x0204C624 and the five
flash-modifying routines stay unreachable.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from typing import Optional, Tuple

# Shim bytes/addresses live in kc02_shim.py (hand-assembled + Ghidra-verified).
try:
    from kc02_shim import (
        CANARY_BYTES,
        CANARY_LEN,
        CANARY_LOAD_ADDR,
        DATA_BUFFER_ADDR,
        DATA_BUFFER_LEN,
        SHIM_BYTES,
        SHIM_LEN,
        SHIM_LOAD_ADDR,
        SPI_READ_FN,
    )
except ImportError as _shim_exc:  # pragma: no cover - only when run out of tree
    raise SystemExit(
        "kc02_shim.py must sit next to kc02_usb.py (run as "
        "'python3 tools/kc02_usb.py ...'): " + str(_shim_exc)
    )

# RAM unlock patch definition (frame stub + mode-record rewrite).
try:
    import kc02_unlock as unlock
except ImportError as _unlock_exc:  # pragma: no cover - only when run out of tree
    raise SystemExit(
        "kc02_unlock.py must sit next to kc02_usb.py (run as "
        "'python3 tools/kc02_usb.py ...'): " + str(_unlock_exc)
    )

# --- pyusb import (kept non-fatal so py_compile works without pyusb installed) -
try:
    import usb.core
    import usb.util
except ImportError as exc:  # pragma: no cover - depends on host environment
    usb = None  # type: ignore[assignment]
    _USB_IMPORT_ERROR: Optional[BaseException] = exc
else:
    _USB_IMPORT_ERROR = None

# --- Safety-critical constants (do not make these configurable) --------------
VENDOR_OPCODE = 0xCD           # CDB[0]: dispatcher routes to the call path
MEMORY_READ_FN = 0x0204C624    # CDB[1..4]: the ONLY permitted function pointer
CALLBACK_SKIP = 0xFFFFFFFF     # CDB[9..12]: skip callback (0 would CALL addr 0)
CALLBACK_ARG = 0               # CDB[13..15]: u24 callback arg (unused)
READ_DIRECTION = 0x80          # bmCBWFlags: device -> host
WRITE_DIRECTION = 0x00         # bmCBWFlags: host -> device (RAM scratch only)
CACHE_CLEAN_FN = 0x0202B058    # D-cache clean(addr, len) - safe, RAM-only helper
CACHE_INVALIDATE_FN = 0x0202B0D4   # D-cache invalidate (CPU-read coherence)

# --- stock "usb device" exit (0x0200d450) restore probes -------------------
# The exit = flag=0 + four calls; one of them releases the lock screen's
# text/HUD suppression.  Probe them individually through an arg-forcing
# tail-call stub (the write-callback channel passes (addr,len,arg), so callees
# needing a fixed r3 must go through the stub).
USB_EXIT_FN = 0x0200D450          # whole exit (restore-all; may power USB down)
RESTORE_PANEL_1 = 0x02004588      # exit callee with r3=1 (output/panel mode)
RESTORE_DISP_0 = 0x02034EE4       # exit callee with r3=0 (display toggle)
RESTORE_C_FN = 0x020403EC         # exit callee, no args
RESTORE_D_FN = 0x0204A9A4         # exit callee, no args (probable USB teardown)
PROBE_STUB_ADDR = 0x020D9028      # 8-byte stub slot (unlock staging window)

# Flash-modifying routines that must never be reachable through this transport.
FORBIDDEN_FUNCTIONS = frozenset(
    {
        0x0203D584,  # 4 KB SECTOR ERASE
        0x0203D454,  # PAGE PROGRAM
        0x0203D4E8,  # PAGE PROGRAM
        0x0203D25C,  # WREN (write-enable)
        0x02002418,  # DestBin.bin upgrade task (erase + program)
    }
)

# --- RAM scratch: the ONLY addresses this tool may write to ------------------
# Proven-writable, cache-cleanable RAM window (0x02044000..0x027FEC00); above
# the uncached low SRAM (< 0x44000) that cache_clean 0x0202B058 refuses, and
# clear of every RAM address this project has identified as in use.
RAM_WINDOW_LO = 0x02044000
RAM_WINDOW_HI = 0x027FEC00

# --- Poke window: the subset of RAM 'poke'/'unlock-usb' may write -----------
# The app's BSS/globals/heap live at/above 0x020c869c (0x020ccd50, 0x020d416c,
# 0x020d4173, ...); 0x020c0000 is the conservative floor this project uses for
# "writable app RAM".  Below 0x020c0000 is flash/rodata (read-only XIP) or the
# uncached low SRAM, and 0x8040/0x8250-style MMIO/SPR space is nowhere near.
PRINT_LUT = (0x0207C640, 0x02082964)   # 63x63 u32 print-style LUT (file 0x7EC40)
RAM_POKE_LO = unlock.RAM_POKE_LO
RAM_POKE_HI = unlock.RAM_POKE_HI
MAX_POKE_LEN = 0x1000           # same bounded chunk as read_memory

# Per-blob windows. A write must fit ENTIRELY inside one of these.
WRITABLE_WINDOWS: Tuple[Tuple[int, int], ...] = (
    (SHIM_LOAD_ADDR, SHIM_LOAD_ADDR + SHIM_LEN),
    (CANARY_LOAD_ADDR, CANARY_LOAD_ADDR + CANARY_LEN),
    (DATA_BUFFER_ADDR, DATA_BUFFER_ADDR + DATA_BUFFER_LEN),
    (0x020D9000, 0x020D9200),   # unlock frame stub + replacement mode record
    (0x020D9200, 0x020D9240),   # usb-hoist trampoline (36 B + margin)
    (0x020D9400, 0x020D9488),   # childlock gate/seq stubs + SEQ/FLAG bytes
    (0x020CCD50, 0x020CCD5C),   # ui_mode_pending + ui_mode_active_rec slots
    (0x020CCD84, 0x020CCD88),   # ui_mode_table[10] slot (unlock-usb)
    (0x020CCD8C, 0x020CCD90),   # usb-screen-active gate (unlock-usb text restore)
    (0x020D41C4, 0x020D41C8),   # ui_shutter_mode byte (stylize gate force)
)

# Overall scratch extent (all writable windows must be inside it).
SCRATCH_WINDOW = (
    min(lo for lo, _ in WRITABLE_WINDOWS),
    max(hi for _, hi in WRITABLE_WINDOWS),
)

# Callbacks this tool may put in CDB[9..12].
ALLOWED_CALLBACKS = frozenset(
    {CALLBACK_SKIP, CACHE_CLEAN_FN, CACHE_INVALIDATE_FN, SHIM_LOAD_ADDR, CANARY_LOAD_ADDR,
     USB_EXIT_FN, RESTORE_PANEL_1, RESTORE_DISP_0, RESTORE_C_FN, RESTORE_D_FN, PROBE_STUB_ADDR}
)

# Addresses load_scratch() may be pointed at (subset of WRITABLE_WINDOWS).
LOADABLE_ADDRS = (SHIM_LOAD_ADDR, CANARY_LOAD_ADDR, DATA_BUFFER_ADDR,
                  0x020D9000, 0x020D9200, 0x020D9400,
                  0x020CCD50, 0x020CCD58, 0x020CCD84,
                  0x020CCD8C)

# Code-region patch sites the poke guard admits (exact address+length match only).
CODE_PATCH_SITES = unlock.CODE_PATCH_SITES

# --- Self-checks on the safety constants (fail at import, not at the device) --
assert MEMORY_READ_FN not in FORBIDDEN_FUNCTIONS, "safety constant mismatch"
assert CALLBACK_SKIP not in FORBIDDEN_FUNCTIONS
assert CACHE_CLEAN_FN not in FORBIDDEN_FUNCTIONS
assert SPI_READ_FN not in FORBIDDEN_FUNCTIONS, "shim target must be a read routine"
assert not (ALLOWED_CALLBACKS & FORBIDDEN_FUNCTIONS), "forbidden callback allowed"
assert RAM_WINDOW_LO <= SCRATCH_WINDOW[0] < SCRATCH_WINDOW[1] <= RAM_WINDOW_HI
assert SCRATCH_WINDOW[0] >= 0x44000, "scratch must not overlap uncached low SRAM"
assert RAM_WINDOW_LO <= RAM_POKE_LO < RAM_POKE_HI <= RAM_WINDOW_HI, \
    "poke window must sit inside the proven-writable RAM window"
assert RAM_POKE_LO >= 0x020C0000, "poke must not reach flash/rodata below 0x020c0000"
assert RAM_POKE_LO > 0x8250 and RAM_POKE_LO > 0x8040, "poke must not reach MMIO"
assert RAM_POKE_LO <= SCRATCH_WINDOW[0] and SCRATCH_WINDOW[1] <= RAM_POKE_HI, \
    "shim scratch must live inside the poke window (so one guard covers both)"
for _lo, _hi in WRITABLE_WINDOWS:
    assert RAM_WINDOW_LO <= _lo < _hi <= RAM_WINDOW_HI, "window outside RAM range"
    assert _hi - _lo <= 0x2000, "write window unreasonably large"
for _addr in LOADABLE_ADDRS:
    assert any(lo <= _addr < hi for lo, hi in WRITABLE_WINDOWS), _addr

# 512-byte SPI chunks: a multiple of 16, so the SPI reader's DMA round-up
# (0x0203D198 rounds the length up to a multiple of 16) can never write past
# the end of the 512-byte data buffer.
SPI_CHUNK = 512
assert SPI_CHUNK == DATA_BUFFER_LEN and SPI_CHUNK % 16 == 0
FLASH_SIZE = 0x400000          # 4 MB; the callback argument is 24-bit
MAX_FLASH_OFFSET = (1 << 24) - 1

# --- Device / transport constants --------------------------------------------
DEFAULT_VID = 0x0219
DEFAULT_PID = 0x3280
INTERFACE_NUMBER = 4
EP_IN = 0x81
EP_OUT = 0x01
BULK_MAX_PACKET = 512

CBW_SIGNATURE = b"USBC"
CSW_SIGNATURE = b"USBS"
CBW_LENGTH = 31
CSW_LENGTH = 13
CSW_STATUS_PASS = 0

CHUNK_SIZE = 0x1000            # bounded chunk for read_memory
DEFAULT_TIMEOUT_MS = 5000
MAX_READ_LEN = 64 * 1024 * 1024  # sanity cap on a single client read request

# Known-writable RAM window (boot-time zero-fill region), bounded and explicit.
RAM_DUMP_START = 0x020C869C
RAM_DUMP_END = 0x02100000

# Flash-window probe target and expected magic.
PROBE_ADDR = 0x01FFDA04
PROBE_MAGIC = b"BLDR"


# --- Errors ------------------------------------------------------------------
class Kc02Error(Exception):
    """Base class for clear, user-facing failures."""


class Kc02SafetyError(Kc02Error):
    """A code path attempted to leave the permitted envelope."""


class Kc02NotFoundError(Kc02Error):
    """No matching USB device was found."""


class Kc02TransportError(Kc02Error):
    """A USB/BOT transfer failed."""


# --- Safety guards (the choke point for the whole invariant) -----------------
def _require_function_pointer(fn: int) -> None:
    """Reject any function pointer other than the stock memory transfer routine."""
    if fn in FORBIDDEN_FUNCTIONS:
        raise Kc02SafetyError(
            f"refusing to call flash-modifying routine 0x{fn:08X} "
            f"(see FORBIDDEN_FUNCTIONS)"
        )
    if fn != MEMORY_READ_FN:
        raise Kc02SafetyError(
            f"refusing to call function pointer 0x{fn:08X}; the only permitted "
            f"target is the stock memory-transfer routine 0x{MEMORY_READ_FN:08X}"
        )


def _require_callback(callback: int) -> None:
    """Reject any callback other than skip / D-cache clean / our two RAM stubs."""
    if callback in FORBIDDEN_FUNCTIONS:
        raise Kc02SafetyError(
            f"refusing callback pointer 0x{callback:08X}: flash-modifying routine"
        )
    if callback not in ALLOWED_CALLBACKS:
        allowed = ", ".join(f"0x{c:08X}" for c in sorted(ALLOWED_CALLBACKS))
        raise Kc02SafetyError(
            f"refusing callback pointer 0x{callback:08X}; permitted callbacks are "
            f"{allowed} (skip / D-cache clean / the loaded shim and canary)"
        )


def _require_scratch_write(address: int, length: int) -> None:
    """Reject any host->device write that is not fully inside a scratch window."""
    if length <= 0:
        raise Kc02SafetyError(f"refusing non-positive write length: {length}")
    if not 0 <= address <= 0xFFFFFFFF:
        raise Kc02SafetyError(f"write address out of 32-bit range: 0x{address:X}")
    end = address + length
    if end > 0x100000000:
        raise Kc02SafetyError(
            f"write 0x{address:08X}+{length} runs past the 32-bit address space"
        )
    if unlock.is_code_patch_site(address, length):
        # Same documented exception as _require_poke_write: an EXACT match of an
        # enumerated code patch site passes everywhere (4-byte sites only).
        return
    if PRINT_LUT[0] <= address and end <= PRINT_LUT[1]:
        return
    if not (SCRATCH_WINDOW[0] <= address and end <= SCRATCH_WINDOW[1]):
        raise Kc02SafetyError(
            f"refusing to write 0x{address:08X}..0x{end:08X}: outside the "
            f"designated RAM scratch window "
            f"0x{SCRATCH_WINDOW[0]:08X}..0x{SCRATCH_WINDOW[1]:08X}"
        )
    for lo, hi in WRITABLE_WINDOWS:
        if lo <= address and end <= hi:
            if lo < 0x44000:
                raise Kc02SafetyError(
                    f"refusing to write below 0x44000 (uncached low SRAM): "
                    f"0x{address:08X}"
                )
            return
    raise Kc02SafetyError(
        f"refusing to write 0x{address:08X}..0x{end:08X}: it does not fit inside "
        "any single designated scratch window ("
        + ", ".join(f"0x{lo:08X}..0x{hi:08X}" for lo, hi in WRITABLE_WINDOWS)
        + ")"
    )


def _require_poke_write(address: int, length: int) -> None:
    """Reject any host->device write that is not fully inside the RAM poke window.

    The poke window (0x020c0000..0x027FEC00) is the app's writable RAM: BSS,
    globals (0x020ccd50/0x020d416c/... ), heap and stack.  It deliberately
    excludes:
      * anything below 0x020c0000 (flash/rodata XIP, read-only, and the
        uncached low SRAM),
      * every MMIO/SPR address (0x8040/0x8250 USB scratch, 0x8900/0x8910 LCDC,
        0x20000002/0x20000003 device registers, 0x9xxx/0xa04x SPRs),
      * the SPI flash controller and the forbidden flash-update routines.
    A write that crosses either boundary raises instead of being clamped.

    The ONE documented exception is an EXACT match of an enumerated code patch
    site in ``kc02_unlock.CODE_PATCH_SITES`` (currently a single 4-byte site at
    0x02000404, the per-frame hook's first instruction).  It is admitted by
    (address, length) equality only - never by a widened range - so no other
    address in the code region 0x02000000..0x0207B7FF is reachable this way.
    """
    if length <= 0:
        raise Kc02SafetyError(f"refusing non-positive write length: {length}")
    if length > MAX_POKE_LEN:
        raise Kc02SafetyError(
            f"refusing a {length}-byte poke; the cap is {MAX_POKE_LEN} bytes"
        )
    if not 0 <= address <= 0xFFFFFFFF:
        raise Kc02SafetyError(f"write address out of 32-bit range: 0x{address:X}")
    end = address + length
    if end > 0x100000000:
        raise Kc02SafetyError(
            f"write 0x{address:08X}+{length} runs past the 32-bit address space"
        )
    if address % 4 or end % 4:
        raise Kc02SafetyError(
            f"refusing an unaligned poke 0x{address:08X}..0x{end:08X}; "
            f"the app reads these locations as 32-bit words"
        )
    if unlock.is_code_patch_site(address, length):
        # Exact enumerated code patch site (kc02_unlock.CODE_PATCH_SITES):
        # today that is only the per-frame USB/BOT hoist's single instruction
        # at 0x02000404.  Nothing else in the code region can be named.
        return
    if PRINT_LUT[0] <= address and end <= PRINT_LUT[1]:
        return
    if not (RAM_POKE_LO <= address and end <= RAM_POKE_HI):
        raise Kc02SafetyError(
            f"refusing to write 0x{address:08X}..0x{end:08X}: outside the "
            f"writable app-RAM window 0x{RAM_POKE_LO:08X}..0x{RAM_POKE_HI:08X} "
            f"and not one of the enumerated code patch sites ("
            f"{', '.join(f'0x{s:08X}+{l}' for s, l, _b, _lbl in unlock.CODE_PATCH_SITES)}); "
            f"flash, SPI-MMIO and SPR space are not reachable through it"
        )


def _require_poke_payload(blob: bytes) -> None:
    """Refuse a poke whose bytes embed a flash-modifying routine's address.

    Poking *data* cannot itself write flash, but a RAM function-pointer slot is
    exactly what this patch area holds (mode table / event-hook table / task
    table), and a value equal to one of FORBIDDEN_FUNCTIONS could be dispatched
    later by the firmware.  Keeping those five addresses out of the RAM this
    tool writes keeps "forbidden routines unreachable" true of the bytes left
    behind, not just of the CBW that was sent.
    """
    forbidden_words = {struct.pack("<I", f) for f in FORBIDDEN_FUNCTIONS}
    for off in range(0, max(0, len(blob) - 3)):
        if blob[off:off + 4] in forbidden_words:
            raise Kc02SafetyError(
                "refusing a poke whose bytes contain a flash-modifying "
                f"routine address at +0x{off:X}"
            )


def _require_read_only_call(fn: int, callback: int) -> None:
    """Reject anything that is not the stock memory-transfer routine, callback skipped.

    Kept as the choke point for the legacy read_memory() path.
    """
    _require_function_pointer(fn)
    if callback != CALLBACK_SKIP:
        raise Kc02SafetyError(
            f"refusing callback pointer 0x{callback:08X}; it must be "
            f"0x{CALLBACK_SKIP:08X} (callback skipped)"
        )


def _build_cbw(
    tag: int,
    fn: int,
    direction: int,
    buffer: int,
    data_length: int,
    callback: int,
    callback_arg: int,
) -> bytes:
    """Build a 31-byte BOT CBW for one vendor 0xCD transaction.

    Every CBW this module can ever emit passes through here, and every field is
    re-validated first: the function pointer must be the stock memory transfer
    routine, the callback must be one of the three permitted targets, the
    direction must be a known bmCBWFlags value, and a host->device transfer is
    only possible into the designated RAM scratch window.
    """
    _require_function_pointer(fn)
    _require_callback(callback)

    if direction not in (READ_DIRECTION, WRITE_DIRECTION):
        raise Kc02TransportError(f"invalid bmCBWFlags: 0x{direction:02X}")
    if not 0 <= data_length <= 0xFFFFFFFF:
        raise Kc02TransportError(f"invalid data length: {data_length}")
    if not 0 <= callback_arg <= 0xFFFFFF:
        raise Kc02TransportError(
            f"callback argument 0x{callback_arg:X} does not fit in the 24-bit field"
        )
    if not 0 <= buffer <= 0xFFFFFFFF:
        raise Kc02TransportError(f"invalid memory address: 0x{buffer:X}")
    if direction == WRITE_DIRECTION:
        # No arbitrary-address write: hard-bound it to the RAM scratch.
        _require_scratch_write(buffer, data_length)

    header = struct.pack(
        "<4sIIBBB",
        CBW_SIGNATURE,
        tag & 0xFFFFFFFF,
        data_length,
        direction,
        0,                # CBWLUN
        16,               # CBWCBLength
    )
    cdb = (
        bytes([VENDOR_OPCODE])
        + struct.pack("<I", fn)                      # CDB[1..4]  fn
        + struct.pack("<I", buffer)                  # CDB[5..8]  buffer
        + struct.pack("<I", callback)                # CDB[9..12] callback
        + struct.pack("<I", callback_arg)[:3]        # CDB[13..15] u24 arg
    )
    assert len(cdb) == 16, "CDB must be exactly 16 bytes"
    cbw = header + cdb
    assert len(cbw) == CBW_LENGTH, "CBW must be exactly 31 bytes"
    return cbw


def _build_read_cbw(tag: int, address: int, data_length: int) -> bytes:
    """Build a 31-byte BOT CBW for a CPU-memory READ.

    The function pointer and callback are fixed constants and are validated
    before packing. Direction is hard-coded to IN (0x80): this function cannot
    produce an OUT (write) transfer.
    """
    _require_read_only_call(MEMORY_READ_FN, CALLBACK_SKIP)
    if not 0 <= address <= 0xFFFFFFFF:
        raise Kc02TransportError(f"invalid memory address: 0x{address:X}")
    return _build_cbw(
        tag, MEMORY_READ_FN, READ_DIRECTION, address, data_length,
        CALLBACK_SKIP, CALLBACK_ARG,
    )


# --- Device ------------------------------------------------------------------
class Kc02Device:
    """USB handle for the KC02 vendor mass-storage interface.

    Reads CPU memory freely; the only writes it can issue are hard-bounded to
    the designated RAM scratch window (see ``load_scratch``).
    """

    def __init__(
        self,
        vid: int = DEFAULT_VID,
        pid: int = DEFAULT_PID,
        timeout_ms: int = DEFAULT_TIMEOUT_MS,
    ) -> None:
        self.vid = vid
        self.pid = pid
        self.timeout_ms = timeout_ms
        self._dev = None
        self._claimed = False
        self._tag = 0

    # -- lifecycle --
    def open(self) -> "Kc02Device":
        """Find the device, detach the kernel driver, and claim interface 4."""
        if usb is None:
            raise Kc02Error(
                "pyusb is not installed. Install it with 'pip install pyusb' and "
                "ensure a libusb backend is present "
                f"(import error: {_USB_IMPORT_ERROR})."
            )

        try:
            dev = usb.core.find(idVendor=self.vid, idProduct=self.pid)
        except usb.core.NoBackendError as exc:
            raise Kc02Error(
                "no libusb backend found; install libusb (Linux: "
                "'libusb-1.0-0') and retry"
            ) from exc
        except usb.core.USBError as exc:
            raise Kc02Error(_explain_usb_error(exc, self.vid, self.pid)) from exc

        if dev is None:
            raise Kc02NotFoundError(
                f"no KC02 device found for VID:PID {self.vid:04x}:{self.pid:04x}. "
                "Check the camera is connected in the right mode "
                "(0x0219:0x3280 or 0x1908:0x3283)."
            )

        # Detach usb-storage from interface 4 before we claim it. Different
        # pyusb/libusb builds expose this on different objects; try them all.
        for _try in (
            lambda: dev.set_auto_detach_kernel_driver(True),
            lambda: usb.util.detach_kernel_driver(dev, INTERFACE_NUMBER),
            lambda: dev.detach_kernel_driver(INTERFACE_NUMBER),
            lambda: dev._ctx.backend.detach_kernel_driver(
                dev._ctx.handle, dev._ctx.device, INTERFACE_NUMBER),
        ):
            try:
                _try()
                break
            except Exception:
                continue
        else:
            print("warning: could not pre-detach kernel driver; claim may fail",
                  file=sys.stderr)

        try:
            dev.set_configuration()
        except usb.core.USBError as exc:
            raise Kc02Error(_explain_usb_error(exc, self.vid, self.pid)) from exc

        try:
            usb.util.claim_interface(dev, INTERFACE_NUMBER)
        except usb.core.USBError as exc:
            raise Kc02Error(
                f"failed to claim interface {INTERFACE_NUMBER}: "
                f"{_explain_usb_error(exc, self.vid, self.pid)}"
            ) from exc

        self._dev = dev
        self._claimed = True
        return self

    def close(self) -> None:
        """Release interface 4 and try to hand it back to the kernel driver."""
        if self._dev is None:
            return
        if self._claimed:
            try:
                usb.util.release_interface(self._dev, INTERFACE_NUMBER)
            except usb.core.USBError:
                pass
            # Restore the stock usb-storage driver if we detached it.
            for _try in (
                lambda: self._dev.attach_kernel_driver(INTERFACE_NUMBER),
                lambda: usb.util.attach_kernel_driver(self._dev, INTERFACE_NUMBER),
                lambda: self._dev._ctx.backend.attach_kernel_driver(
                    self._dev._ctx.handle, self._dev._ctx.device, INTERFACE_NUMBER),
            ):
                try:
                    _try()
                    break
                except Exception:
                    continue
            self._claimed = False
        usb.util.dispose_resources(self._dev)
        self._dev = None

    def __enter__(self) -> "Kc02Device":
        return self.open()

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    # -- transport helpers --
    def _next_tag(self) -> int:
        self._tag = (self._tag + 1) & 0xFFFFFFFF
        return self._tag

    def _read_exact(self, endpoint: int, length: int) -> bytes:
        """Read exactly `length` bytes from a bulk IN endpoint, looping over
        short transfers, with a timeout."""
        assert self._dev is not None, "device not open"
        buf = bytearray()
        while len(buf) < length:
            want = length - len(buf)
            try:
                chunk = self._dev.read(endpoint, want, timeout=self.timeout_ms)
            except usb.core.USBError as exc:
                raise Kc02TransportError(
                    f"bulk read failed on ep 0x{endpoint:02x} after {len(buf)}/"
                    f"{length} bytes: {exc}"
                ) from exc
            if not chunk:
                raise Kc02TransportError(
                    f"short read on ep 0x{endpoint:02x}: got {len(buf)}/{length} "
                    "bytes then the device returned no data"
                )
            buf.extend(chunk)
        return bytes(buf)

    def _bulk_out(self, endpoint: int, data: bytes) -> None:
        assert self._dev is not None, "device not open"
        try:
            written = self._dev.write(endpoint, data, timeout=self.timeout_ms)
        except usb.core.USBError as exc:
            raise Kc02TransportError(
                f"bulk write failed on ep 0x{endpoint:02x}: {exc}"
            ) from exc
        if written != len(data):
            raise Kc02TransportError(
                f"short write on ep 0x{endpoint:02x}: {written}/{len(data)} bytes"
            )

    def _read_csw(self, tag: int, data_length: int, received: int) -> None:
        """Read and validate the 13-byte CSW for the transaction with `tag`."""
        csw = self._read_exact(EP_IN, CSW_LENGTH)
        signature, echoed_tag, residue, status = struct.unpack("<4sIIB", csw)
        if signature != CSW_SIGNATURE:
            raise Kc02TransportError(
                f"bad CSW signature {signature!r}, expected {CSW_SIGNATURE!r}"
            )
        if echoed_tag != tag:
            raise Kc02TransportError(
                f"CSW tag mismatch: echoed 0x{echoed_tag:08X}, sent 0x{tag:08X}"
            )
        if status != CSW_STATUS_PASS:
            raise Kc02TransportError(
                f"device reported CSW status {status} (residue={residue})"
            )
        if residue and residue != data_length - received:
            print(
                f"warning: CSW residue {residue} does not match short transfer "
                f"({received}/{data_length})",
                file=sys.stderr,
            )

    def _transfer(self, cdb_address: int, data_length: int) -> bytes:
        """Legacy read path: CPU-memory READ, callback skipped.

        Builds its CBW through _build_read_cbw, so it is structurally read-only.
        """
        assert self._dev is not None, "device not open"
        tag = self._next_tag()
        cbw = _build_read_cbw(tag, cdb_address, data_length)
        self._bulk_out(EP_OUT, cbw)
        data = self._read_exact(EP_IN, data_length) if data_length else b""
        self._read_csw(tag, data_length, len(data))
        return data

    def _transfer_in(
        self,
        buffer: int,
        data_length: int,
        callback: int = CALLBACK_SKIP,
        callback_arg: int = 0,
    ) -> bytes:
        """One vendor transaction, device -> host, with an optional callback.

        `buffer` is any CPU address (this direction only reads).  `callback` is
        validated by _build_cbw; the firmware calls it before sending.
        """
        assert self._dev is not None, "device not open"
        tag = self._next_tag()
        cbw = _build_cbw(
            tag, MEMORY_READ_FN, READ_DIRECTION, buffer, data_length,
            callback, callback_arg,
        )
        self._bulk_out(EP_OUT, cbw)
        data = self._read_exact(EP_IN, data_length) if data_length else b""
        self._read_csw(tag, data_length, len(data))
        return data

    def _transfer_out(
        self,
        buffer: int,
        payload: bytes,
        callback: int = CALLBACK_SKIP,
        callback_arg: int = 0,
    ) -> None:
        """One vendor transaction, host -> device, into RAM scratch only.

        _build_cbw() hard-bounds `buffer` to the designated scratch window, so
        this method cannot write anywhere else in the machine.
        """
        assert self._dev is not None, "device not open"
        if not payload:
            raise Kc02TransportError("refusing an empty host->device transfer")
        tag = self._next_tag()
        cbw = _build_cbw(
            tag, MEMORY_READ_FN, WRITE_DIRECTION, buffer, len(payload),
            callback, callback_arg,
        )
        self._bulk_out(EP_OUT, cbw)
        self._bulk_out(EP_OUT, payload)
        self._read_csw(tag, len(payload), len(payload))

    # -- the one and only exfiltration path --
    def read_memory(self, addr: int, length: int) -> bytes:
        """Read `length` bytes of CPU memory starting at `addr`.

        Chunked through the hard-coded stock READ routine with the callback
        skipped; this path can never write anything.
        """
        if not 0 <= addr <= 0xFFFFFFFF:
            raise Kc02Error(f"address out of 32-bit range: 0x{addr:X}")
        if length < 0:
            raise Kc02Error(f"negative length: {length}")
        if length > MAX_READ_LEN:
            raise Kc02Error(
                f"requested {length} bytes exceeds the {MAX_READ_LEN}-byte cap"
            )
        if addr + length > 0x100000000:
            raise Kc02Error(
                f"read 0x{addr:X}+{length} runs past the 32-bit address space"
            )

        out = bytearray()
        while len(out) < length:
            chunk = min(CHUNK_SIZE, length - len(out))
            out.extend(self._transfer(addr + len(out), chunk))
        return bytes(out)

    # -- guarded RAM scratch writes -------------------------------------------
    def load_scratch(
        self,
        address: int,
        blob: bytes,
        cache_clean: bool = True,
        verify: bool = True,
    ) -> bytes:
        """Copy `blob` into the fixed RAM scratch at `address`.

        `address` must be one of LOADABLE_ADDRS and the whole blob must fit
        inside that designated scratch window, or this raises.  When
        `cache_clean` is set the same transaction's callback slot is
        0x0202B058, i.e. the firmware runs cache_clean(address, len(blob))
        right after receiving the bytes, so instruction/data fetch sees them.

        With `verify` set the bytes are read straight back and compared, so a
        short or misplaced write fails loudly instead of leaving a truncated
        stub in RAM.  Returns the bytes read back (== blob on success).
        """
        if address not in LOADABLE_ADDRS:
            raise Kc02SafetyError(
                f"refusing to load scratch at 0x{address:08X}; permitted targets "
                "are " + ", ".join(f"0x{a:08X}" for a in LOADABLE_ADDRS)
            )
        _require_scratch_write(address, len(blob))
        callback = CACHE_CLEAN_FN if cache_clean else CALLBACK_SKIP
        self._transfer_out(address, bytes(blob), callback=callback, callback_arg=0)
        if not verify:
            return b""
        got = self._transfer(address, len(blob))
        if got != bytes(blob):
            first = next(
                (i for i, (a, b) in enumerate(zip(got, blob)) if a != b),
                None,
            )
            where = f"first difference at +0x{first:X}" if first is not None \
                else f"read back {len(got)} of {len(blob)} bytes"
            raise Kc02TransportError(
                f"RAM scratch read-back mismatch at 0x{address:08X} "
                f"({where}); the stub was not stored intact - not proceeding"
            )
        return got

    def poke(self, address: int, blob: bytes, verify: bool = True, invalidate: bool = False,
             callback: int | None = None) -> bytes:
        """Write `blob` to writable app RAM at `address`, then read it back.

        This is the general bounded poke used for runtime experimentation and
        by ``unlock-usb``.  Unlike ``load_scratch`` it is not restricted to the
        three fixed scratch slots, but every byte is hard-bounded to the RAM
        poke window by ``_require_poke_write``: flash, SPI-MMIO and SPR space
        cannot be named.  The D-cache clean (0x0202B058) runs in the same
        transaction's callback slot, so code written this way is immediately
        visible to instruction fetch.

        With `verify` set the bytes are read straight back and compared.
        Returns the bytes read back (== blob on success).
        """
        if not blob:
            raise Kc02SafetyError("refusing an empty poke")
        _require_poke_write(address, len(blob))
        _require_poke_payload(bytes(blob))
        self._transfer_out(address, bytes(blob), callback=callback or (CACHE_INVALIDATE_FN if invalidate else CACHE_CLEAN_FN),
                           callback_arg=0)
        if not verify:
            return b""
        if address == 0x020CCD50:
            # ui_mode_pending is a SIGNAL slot: the live UI task consumes the
            # value the instant it lands (that is the entire point of writing
            # it), so read-back verification can never match. Trust the write.
            return b""
        got = self._transfer(address, len(blob))
        if got != bytes(blob):
            first = next((i for i, (a, b) in enumerate(zip(got, blob)) if a != b), None)
            where = f"first difference at +0x{first:X}" if first is not None \
                else f"read back {len(got)} of {len(blob)} bytes"
            raise Kc02TransportError(
                f"RAM poke read-back mismatch at 0x{address:08X} ({where}); "
                f"the patch was not stored intact"
            )
        return got

    def load_shim(self) -> None:
        """Write the SPI-read shim into RAM scratch, clean it and read it back."""
        self.load_scratch(SHIM_LOAD_ADDR, SHIM_BYTES, cache_clean=True)

    def load_canary(self) -> None:
        """Write the `l.jr r9` canary into RAM scratch, clean it and read it back."""
        self.load_scratch(CANARY_LOAD_ADDR, CANARY_BYTES, cache_clean=True)

    # -- RAM execution probe ---------------------------------------------------
    def canary(self, pattern: Optional[bytes] = None) -> bytes:
        """Prove RAM executability + I-cache before trusting the shim.

        Writes a known pattern into the 512-byte data buffer, then issues a
        memory READ whose callback slot points at the canary stub.  The canary
        is a single `l.jr r9`, so if RAM execution works the firmware calls it
        (it returns immediately to 0x0204C584) and then sends the data buffer
        back untouched.  A hang means the caller has to power-cycle; a mismatch
        means RAM execution is not safe to rely on.
        """
        payload = pattern if pattern is not None else _canary_pattern()
        if len(payload) != DATA_BUFFER_LEN:
            raise Kc02Error(
                f"canary pattern must be {DATA_BUFFER_LEN} bytes, got {len(payload)}"
            )
        self.load_canary()
        self.load_scratch(DATA_BUFFER_ADDR, payload, cache_clean=True)
        got = self._transfer_in(
            DATA_BUFFER_ADDR, DATA_BUFFER_LEN,
            callback=CANARY_LOAD_ADDR, callback_arg=0,
        )
        return got

    # -- SPI flash read through the RAM shim ----------------------------------
    def flash_read(self, offset: int, length: int, progress=None) -> bytes:
        """Read `length` bytes of SPI flash starting at 24-bit `offset`.

        Each 512-byte chunk is fetched by pointing the callback slot at the RAM
        shim: the firmware calls shim(r3=data_buffer, r4=chunk, r5=chunk
        offset), the shim reshapes that into SPI_read(offset, data_buffer,
        chunk), and the firmware then sends the freshly filled buffer back.
        The host never writes flash; this is a pure read of the SPI bus.
        """
        if not 0 <= offset <= MAX_FLASH_OFFSET:
            raise Kc02Error(
                f"flash offset 0x{offset:X} out of 24-bit range "
                f"(0x0..0x{MAX_FLASH_OFFSET:X})"
            )
        if length < 0:
            raise Kc02Error(f"negative length: {length}")
        if offset + length > MAX_FLASH_OFFSET + 1:
            raise Kc02Error(
                f"flash read 0x{offset:X}+{length} runs past the 24-bit "
                "address space"
            )

        out = bytearray()
        while len(out) < length:
            chunk = min(SPI_CHUNK, length - len(out))
            # Always into the same 512-byte buffer: the SPI reader rounds its
            # DMA length up to a multiple of 16, and 512 % 16 == 0 keeps that
            # round-up inside the buffer.
            out.extend(
                self._transfer_in(
                    DATA_BUFFER_ADDR, chunk,
                    callback=SHIM_LOAD_ADDR, callback_arg=offset + len(out),
                )
            )
            if progress is not None:
                progress(len(out), length)
        return bytes(out)


def _canary_pattern(length: int = DATA_BUFFER_LEN) -> bytes:
    """Deterministic, recognisable fill for the RAM execution probe."""
    return bytes((0xA5 ^ (i * 0x3D)) & 0xFF for i in range(length))


def _explain_usb_error(exc: "usb.core.USBError", vid: int, pid: int) -> str:
    """Turn a libusb exception into an actionable message."""
    text = str(exc)
    lowered = text.lower()
    errno_val = getattr(exc, "errno", None)
    if errno_val in (13, getattr(os, "EACCES", 13)) or "access" in lowered or \
            "permission" in lowered:
        return (
            f"permission denied accessing {vid:04x}:{pid:04x} ({text}). "
            "Add a udev rule granting your user access to this device, or run "
            "with sufficient privileges. The kernel usb-storage driver is "
            "detached automatically once access is available."
        )
    if "busy" in lowered or "resource busy" in lowered:
        return (
            f"device {vid:04x}:{pid:04x} is busy ({text}); another driver or "
            "process may hold interface 4."
        )
    return f"USB error for {vid:04x}:{pid:04x}: {text}"


# --- CLI helpers -------------------------------------------------------------
def _parse_int(text: str) -> int:
    """Parse a decimal or 0x-prefixed hex integer."""
    try:
        if text.lower().startswith("0x"):
            return int(text, 16)
        return int(text, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid integer: {text!r}") from exc


def _hexdump(data: bytes) -> str:
    lines = []
    for off in range(0, len(data), 16):
        chunk = data[off:off + 16]
        hexpart = " ".join(f"{b:02x}" for b in chunk).ljust(16 * 3 - 1)
        printable = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"{off:08x}  {hexpart}  |{printable}|")
    return "\n".join(lines)


def _cmd_read(args: argparse.Namespace) -> int:
    with Kc02Device(args.vid, args.pid, args.timeout_ms) as dev:
        data = dev.read_memory(args.addr, args.length)
    if args.out:
        with open(args.out, "wb") as fh:
            fh.write(data)
        print(f"read {len(data)} bytes from 0x{args.addr:08X} -> {args.out}")
    else:
        print(f"read {len(data)} bytes from 0x{args.addr:08X}:")
        print(_hexdump(data))
    return 0


def _cmd_probe(args: argparse.Namespace) -> int:
    with Kc02Device(args.vid, args.pid, args.timeout_ms) as dev:
        data = dev.read_memory(PROBE_ADDR, len(PROBE_MAGIC))
    hexed = " ".join(f"{b:02X}" for b in data)
    is_magic = data == PROBE_MAGIC
    print(f"probe 0x{PROBE_ADDR:08X}: {hexed}")
    if is_magic:
        print("flash-window magic 'BLDR' (42 4C 44 52) present")
    else:
        print("no 'BLDR' magic at this address")
    return 0


def _cmd_dump_ram(args: argparse.Namespace) -> int:
    length = RAM_DUMP_END - RAM_DUMP_START
    with Kc02Device(args.vid, args.pid, args.timeout_ms) as dev:
        data = dev.read_memory(RAM_DUMP_START, length)
    with open(args.out, "wb") as fh:
        fh.write(data)
    print(
        f"dumped {len(data)} bytes "
        f"[0x{RAM_DUMP_START:08X}..0x{RAM_DUMP_END:08X}) -> {args.out}"
    )
    return 0


def _cmd_shim_info(args: argparse.Namespace) -> int:
    """Offline: print the shim/canary blobs and the scratch layout."""
    print("KC02 SPI-read shim (hand-assembled; see tools/kc02_shim.py)")
    print(f"  shim        : 0x{SHIM_LOAD_ADDR:08X}, {SHIM_LEN} bytes")
    print(f"  canary      : 0x{CANARY_LOAD_ADDR:08X}, {CANARY_LEN} bytes")
    print(f"  data buffer : 0x{DATA_BUFFER_ADDR:08X}, {DATA_BUFFER_LEN} bytes")
    print(f"  SPI chunk   : {SPI_CHUNK} bytes")
    print(f"  scratch     : 0x{SCRATCH_WINDOW[0]:08X}..0x{SCRATCH_WINDOW[1]:08X}"
          f" (inside 0x{RAM_WINDOW_LO:08X}..0x{RAM_WINDOW_HI:08X})")
    print(f"  poke window : 0x{RAM_POKE_LO:08X}..0x{RAM_POKE_HI:08X}"
          f" (RAM only; {MAX_POKE_LEN}-byte cap, word-aligned)")
    print(f"  unlock stub : 0x{unlock.FRAME_STUB_ADDR:08X} "
          f"({unlock.FRAME_STUB_LEN} bytes, mode-10 frame)")
    print(f"  shim calls  : SPI_read 0x{SPI_READ_FN:08X}(offset, dest, count)")
    print(f"  shim bytes  : {' '.join(f'{b:02x}' for b in SHIM_BYTES)}")
    print(f"  canary bytes: {' '.join(f'{b:02x}' for b in CANARY_BYTES)}")
    print("  allowed fn slot       : " + f"0x{MEMORY_READ_FN:08X}")
    print("  allowed callback slots: "
          + ", ".join(f"0x{c:08X}" for c in sorted(ALLOWED_CALLBACKS)))
    print("  forbidden routines    : "
          + ", ".join(f"0x{f:08X}" for f in sorted(FORBIDDEN_FUNCTIONS)))
    return 0


def _parse_poke_payload(args: argparse.Namespace) -> bytes:
    """Turn the poke CLI options into the bytes to write (exactly one source)."""
    sources = [args.hex_bytes is not None, args.bytes_list is not None,
               args.from_file is not None]
    if sum(sources) != 1:
        raise Kc02Error(
            "poke needs exactly one of --hex, --bytes or --from-file"
        )
    if args.hex_bytes is not None:
        text = args.hex_bytes.replace(" ", "").replace(",", "").replace("0x", "")
        if text.startswith("0x"):
            text = text[2:]
        if len(text) == 0 or len(text) % 2:
            raise Kc02Error(f"--hex must be an even number of hex digits: {args.hex_bytes!r}")
        try:
            return bytes.fromhex(text)
        except ValueError as exc:
            raise Kc02Error(f"--hex is not valid hex: {args.hex_bytes!r}") from exc
    if args.bytes_list is not None:
        raw = args.bytes_list.replace(",", " ").replace(" ", " ")
        out = bytearray()
        for tok in raw.split():
            value = _parse_int(tok)
            if not 0 <= value <= 0xFF:
                raise Kc02Error(f"--bytes value {tok!r} is not a byte")
            out.append(value)
        if not out:
            raise Kc02Error("--bytes was empty")
        return bytes(out)
    with open(args.from_file, "rb") as fh:
        return fh.read()


def _cmd_poke(args: argparse.Namespace) -> int:
    """General bounded RAM poke (see _require_poke_write for the hard guard)."""
    payload = _parse_poke_payload(args)
    _require_poke_write(args.addr, len(payload))
    _require_poke_payload(payload)
    if args.dry_run:
        print(f"dry-run: would write {len(payload)} bytes to 0x{args.addr:08X}")
        print(_hexdump(payload))
        return 0
    with Kc02Device(args.vid, args.pid, args.timeout_ms) as dev:
        got = dev.poke(args.addr, payload, verify=not args.no_verify)
    print(f"poke {len(payload)} bytes -> 0x{args.addr:08X}: {_hexdump(payload)}")
    if got:
        print(f"read back identical ({len(got)} bytes)")
    else:
        print("read-back verification skipped (--no-verify)")
    return 0


def _cmd_child_lock(args: argparse.Namespace) -> int:
    """Apply / restore the child-friendly lock (boot-to-camera + adult menu gate).

    Three code words (each an EXACT match of an entry in
    ``kc02_unlock.CODE_PATCH_SITES``) plus a bounded RAM area: two trampolines
    and the two control bytes.  Every write goes through Kc02Device.poke ->
    _require_poke_write, i.e. plain CPU stores with a D-cache clean; no SPI or
    flash routine is involved anywhere.
    """
    adult = {"adult-on": True, "adult-off": False}.get(args.action)

    if adult is not None:
        flag_text = "ON" if adult else "off"
        print(f"child-lock: adult override {flag_text}")
        print(f"  0x{unlock.CHILDLOCK_SEQ_ADDR:08X}  CHILDLOCK_SEQ   (cleared)")
        print(f"  0x{unlock.CHILDLOCK_FLAG_ADDR:08X}  CHILDLOCK_FLAG  "
              f"<- {'1 (menu reachable)' if adult else '0 (menu gated)'}")
        writes = unlock.childlock_adult_plan(adult)
    elif args.restore:
        print("child-lock: restore (three stock code words + zeroed RAM scratch)")
        writes = unlock.childlock_restore_plan()
    else:
        print("child-lock: boot straight into the camera + adult-only Main menu")
        print(f"  boot word        : 0x{unlock.BOOT_MODE_WORD_ADDR:08X} stock "
              f"{unlock.BOOT_MODE_WORD_STOCK.hex()} (b.addi r3,r0,0x6 = Main menu)"
              f" -> {unlock.BOOT_MODE_CODE_BYTES.hex()} (b.addi r3,r0,0x3 = camera)")
        print(f"  menu gate handler: 0x{unlock.MENU_GATE_HANDLER_ADDR:08X} "
              f"key_mode3_power_request_menu (POWER, event 0x24) -> l.j "
              f"0x{unlock.GATE_STUB_ADDR:08X}")
        print(f"  seq tap handler  : 0x{unlock.UP_HANDLER_ADDR:08X} "
              f"mode-3 UP handler (event 0x1b, from the hook list "
              f"0x{unlock.MODE3_HOOK_LIST:08X}) -> l.j 0x{unlock.SEQ_STUB_ADDR:08X}")
        print("  adult action     : tap UP, then press POWER  (or `child-lock "
              "adult-on` for a permanent override)")
        print(f"  live jump        : ui_mode_pending (0x{unlock.MODE_PENDING_ADDR:08X}) = 3 "
              f"- TRANSIENT, jumps the running UI to the camera now")
        writes = unlock.childlock_plan() + unlock.childlock_live_plan()

    print("\nexact writes:")
    for label, addr, blob in writes:
        kind = "RAM " if unlock.RAM_POKE_LO <= addr < unlock.RAM_POKE_HI else "CODE"
        print(f"  {label:24s} {kind} 0x{addr:08X} {len(blob):4d} B  {blob.hex()}")

    for label, addr, blob in writes:
        _require_poke_write(addr, len(blob))
        _require_poke_payload(blob)

    if args.dry_run:
        print("\ndry-run: guards passed, nothing was sent")
        return 0

    with Kc02Device(args.vid, args.pid, args.timeout_ms) as dev:
        if adult is not None:
            for label, addr, blob in writes:
                dev.poke(addr, blob)
                print(f"  wrote {label:24s} 0x{addr:08X} {len(blob):4d} B")
            print(f"  adult override is now {'ON' if adult else 'off'} "
                  f"(RAM only; lasts until power-off)")
            return 0

        # Verify what the three code sites hold before touching them.
        words = [dev.read_memory(site, 4)
                 for site, _l, _s, _p, _lbl in unlock.CHILDLOCK_WORD_SITES]
        state = unlock.childlock_site_state(words)
        for (site, _l, stock, patched, label), got in zip(unlock.CHILDLOCK_WORD_SITES, words):
            print(f"  0x{site:08X} holds {got.hex()} ({label})")
        if state == "unknown":
            raise Kc02Error(
                "the child-lock code sites hold neither the stock words nor this "
                "patch - refusing to write (wrong firmware, or a different patch "
                "is applied); use `child-lock --restore` only on a known state"
            )
        if args.restore:
            if state == "stock":
                print("  the sites already hold the stock words; zeroing the RAM scratch")
            for label, addr, blob in writes:
                dev.poke(addr, blob)
                print(f"  wrote {label:24s} 0x{addr:08X} {len(blob):4d} B")
            print("  restored: the camera boots into the Main menu again "
                  "(power-cycle to be sure).")
            return 0
        if state == "patched":
            print("  the child lock is already applied; re-writing it")
        for label, addr, blob in writes:
            dev.poke(addr, blob)
            print(f"  wrote {label:24s} 0x{addr:08X} {len(blob):4d} B")
        print("  child lock applied: boot goes straight to the camera, the Main "
              "menu needs UP then POWER (or `child-lock adult-on`).\n"
              "  The code words take effect from the next boot; the UI has been "
              "jumped to the camera now via ui_mode_pending.")
    return 0


def _cmd_usb_hoist(args: argparse.Namespace) -> int:
    """Hoist the USB/BOT service out of UI mode 10 into every UI mode.

    Two writes: a 36-byte RAM trampoline at 0x020D9200, then the first
    instruction of the every-mode per-frame hook FUN_02000404 (0x02000404,
    stock ``b.sw -0x4(r1),r9``) replaced by ``l.j 0x020D9200``.  The code write
    goes through the same Kc02Device.poke path as every RAM write, so it is a
    plain CPU store with a D-cache clean - no SPI/flash routine is involved.
    """
    print("USB/BOT hoist patch (RAM stub + one allowlisted code word):")
    print(f"  per-frame hook   : 0x{unlock.PER_FRAME_HOOK_ADDR:08X} "
          f"FUN_02000404 (only caller 0x020082a4 in the mode walker "
          f"FUN_02008188)")
    print(f"  code patch site  : 0x{unlock.PER_FRAME_HOOK_ADDR:08X}+"
          f"{len(unlock.CODE_PATCH_BYTES)} -> l.j 0x{unlock.HOIST_STUB_ADDR:08X} "
          f"(exact allowlist match)")
    print(f"  RAM stub         : 0x{unlock.HOIST_STUB_ADDR:08X} "
          f"({unlock.HOIST_STUB_LEN} bytes)")
    print(f"  BOT service      : 0x{unlock.BOT_SERVICE_FN:08X} "
          f"(pure poll/parse/dispatch; panel feed 0x{unlock.BOT_WRAPPER_FN:08X} "
          f"is NOT called)")

    writes = unlock.hoist_plan()
    print("\nexact writes:")
    for label, addr, blob in writes:
        kind = "RAM " if unlock.RAM_POKE_LO <= addr < unlock.RAM_POKE_HI else "CODE"
        print(f"  {label:12s} {kind} 0x{addr:08X} {len(blob):4d} B  {blob.hex()}")

    def guard_all() -> None:
        for label, addr, blob in writes:
            _require_poke_write(addr, len(blob))
            _require_poke_payload(blob)

    guard_all()

    if args.dry_run:
        print("\ndry-run: guards passed, nothing was sent")
        return 0

    with Kc02Device(args.vid, args.pid, args.timeout_ms) as dev:
        # Verify the stock instruction is still there before overwriting it.
        got = dev.read_memory(unlock.PER_FRAME_HOOK_ADDR, len(unlock.CODE_PATCH_BYTES))
        if got != unlock.PER_FRAME_HOOK_STOCK:
            raise Kc02Error(
                f"code patch site 0x{unlock.PER_FRAME_HOOK_ADDR:08X} holds "
                f"{got.hex()}, expected stock "
                f"{unlock.PER_FRAME_HOOK_STOCK.hex()} - wrong firmware or "
                f"already patched; refusing to write"
            )
        print(f"\n  site holds stock bytes {got.hex()} (expected)")
        for label, addr, blob in writes:
            dev.poke(addr, blob)
            print(f"  wrote {label:12s} 0x{addr:08X} {len(blob):4d} B "
                  f"(read back identical)")
        print("  hoist applied: the BOT transport is now serviced once per UI "
              "frame in every mode; power-cycle to revert.")
    return 0


def _cmd_unlock_usb(args: argparse.Namespace) -> int:
    """Apply the RAM-only USB-unlock patch (see tools/kc02_unlock.py).

    Reads the stock Main-menu mode record off the running device, builds the
    replacement record + frame stub locally, then writes the five bounded RAM
    slots.  Nothing here can touch flash: every write goes through
    Kc02Device.poke -> _require_poke_write -> the RAM poke window.
    """
    print("USB-unlock patch (RAM only, no flash):")
    print(f"  lock screen      : app mode {unlock.USBMODE_INDEX} \"usb device\" "
          f"(record 0x020c339c)")
    print(f"  RAM frame stub   : 0x{unlock.FRAME_STUB_ADDR:08X} "
          f"({unlock.FRAME_STUB_LEN} bytes)")
    print(f"  replacement rec  : 0x{unlock.REC_ADDR:08X} "
          f"({unlock.MAINMENU_REC_LEN} bytes)")
    print(f"  mode table slot  : 0x{unlock.MODE_TABLE_SLOT:08X} "
          f"<- 0x{unlock.REC_ADDR:08X}")
    print(f"  active rec slot  : 0x{unlock.MODE_ACTIVE_REC_ADDR:08X} "
          f"<- 0x{unlock.REC_ADDR:08X}")
    print(f"  pending mode     : 0x{unlock.MODE_PENDING_ADDR:08X} "
          f"<- {unlock.USBMODE_INDEX}")

    def emit(writes) -> None:
        for label, addr, blob in writes:
            kind = "RAM " if unlock.RAM_POKE_LO <= addr < unlock.RAM_POKE_HI else "CODE"
            print(f"  {label:18s} {kind} 0x{addr:08X} {len(blob):4d} B  {blob.hex()}")

    def combined(five_writes):
        """Hoist first (it keeps the transport alive in every mode), then unlock."""
        if not args.hoist:
            return list(five_writes)
        return unlock.hoist_plan() + list(five_writes)

    if args.hoist:
        print(f"  + hoist          : per-frame hook 0x{unlock.PER_FRAME_HOOK_ADDR:08X}"
              f"+4 -> l.j 0x{unlock.HOIST_STUB_ADDR:08X}, stub "
              f"{unlock.HOIST_STUB_LEN} B at 0x{unlock.HOIST_STUB_ADDR:08X}")
        print("  + combination    : mode 10's stub keeps calling 0x0204abc8 AND "
              "the hoist calls 0x0204d0a8 every frame; the BOT poll is "
              "idempotent when no CBW is waiting")

    if args.rec:
        # Fully offline: use a stock-record dump instead of reading the device.
        with open(args.rec, "rb") as fh:
            stock = fh.read()
        print(f"\nusing stock Main-menu record from {args.rec} "
              f"({len(stock)} bytes)")
        print("  stock record : " + " ".join(
            f"{w:08x}" for w in struct.unpack("<6I", stock[:24])))
        writes = combined(unlock.plan(stock))
        for label, addr, blob in writes:
            _require_poke_write(addr, len(blob))
            _require_poke_payload(blob)
        print("\nexact writes (offline; no device was opened):")
        emit(writes)
        return 0

    with Kc02Device(args.vid, args.pid, args.timeout_ms) as dev:
        print(f"\nreading stock Main-menu record "
              f"0x{unlock.MAINMENU_REC_ADDR:08X} "
              f"({unlock.MAINMENU_REC_LEN} bytes) ...")
        stock = dev.read_memory(unlock.MAINMENU_REC_ADDR, unlock.MAINMENU_REC_LEN)
        writes = combined(unlock.plan(stock))
        for label, addr, blob in writes:
            _require_poke_write(addr, len(blob))
            _require_poke_payload(blob)
        print("  stock record : " + " ".join(
            f"{w:08x}" for w in struct.unpack("<6I", stock[:24])))

        if args.dry_run:
            print("\ndry-run: these are the exact writes that would be sent:")
            emit(writes)
            print("nothing was written")
            return 0

        if args.hoist:
            got = dev.read_memory(unlock.PER_FRAME_HOOK_ADDR,
                                  len(unlock.CODE_PATCH_BYTES))
            if got != unlock.PER_FRAME_HOOK_STOCK:
                raise Kc02Error(
                    f"code patch site 0x{unlock.PER_FRAME_HOOK_ADDR:08X} holds "
                    f"{got.hex()}, expected stock "
                    f"{unlock.PER_FRAME_HOOK_STOCK.hex()}; refusing to write"
                )

        for label, addr, blob in writes:
            dev.poke(addr, blob)
            print(f"  wrote {label:18s} 0x{addr:08X} {len(blob):4d} B "
                  f"(read back identical)")
        print("  patch applied. the camera UI should be live while USB/MSC "
              "stays attached; power-cycle to revert.")
    return 0


def _cmd_selftest(args: argparse.Namespace) -> int:
    """Offline: verify CBW construction, field encoding and safety guards."""
    return selftest()

def _cmd_load_shim(args: argparse.Namespace) -> int:
    """Write the shim + canary into the fixed RAM scratch (no flash access)."""
    with Kc02Device(args.vid, args.pid, args.timeout_ms) as dev:
        dev.load_shim()
        dev.load_canary()
    print(f"loaded {SHIM_LEN}-byte shim   at 0x{SHIM_LOAD_ADDR:08X} "
          f"(D-cache cleaned via 0x{CACHE_CLEAN_FN:08X}, read back OK)")
    print(f"loaded {CANARY_LEN}-byte canary at 0x{CANARY_LOAD_ADDR:08X} "
          f"(D-cache cleaned via 0x{CACHE_CLEAN_FN:08X}, read back OK)")
    print("next: 'canary' to prove RAM execution, then 'flash-read'")
    return 0


def _cmd_canary(args: argparse.Namespace) -> int:
    """Invoke the `l.jr r9` canary through the callback slot and read back."""
    with Kc02Device(args.vid, args.pid, args.timeout_ms) as dev:
        got = dev.canary()
    expected = _canary_pattern()
    if got == expected:
        print(f"canary OK: {len(got)}-byte RAM buffer round-tripped unchanged "
              f"through an l.jr r9 stub at 0x{CANARY_LOAD_ADDR:08X}.")
        print("RAM execution and the (fresh-region) I-cache path look usable.")
        return 0
    diffs = sum(1 for a, b in zip(got, expected) if a != b)
    print(f"canary FAILED: {diffs}/{len(expected)} bytes differ from the pattern "
          f"written to 0x{DATA_BUFFER_ADDR:08X}", file=sys.stderr)
    print("  the callback did not return cleanly, or the buffer moved; do not "
          "trust the shim", file=sys.stderr)
    return 1


def _cmd_flash_read(args: argparse.Namespace) -> int:
    """Read SPI flash through the RAM shim; never writes flash."""
    if args.length < 0:
        raise Kc02Error(f"negative length: {args.length}")
    total = args.length

    def progress(done: int, want: int) -> None:
        if not args.quiet and want:
            print(f"\r{done}/{want} bytes ({100.0 * done / want:5.1f}%)",
                  end="", file=sys.stderr, flush=True)

    with Kc02Device(args.vid, args.pid, args.timeout_ms) as dev:
        dev.load_shim()
        data = dev.flash_read(args.offset, args.length, progress=progress)
    if not args.quiet and total:
        print(file=sys.stderr)

    if args.out:
        with open(args.out, "wb") as fh:
            fh.write(data)
        print(f"read {len(data)} bytes of SPI flash from 0x{args.offset:X} "
              f"-> {args.out}")
    else:
        print(f"read {len(data)} bytes of SPI flash from 0x{args.offset:X}:")
        print(_hexdump(data))
    return 0


# --- Offline self-test --------------------------------------------------------
def selftest() -> int:
    """Exercise the CBW builder and every guard without touching a device."""
    failures: list = []

    def check(cond: bool, msg: str) -> None:
        if not cond:
            failures.append(msg)

    def raises(fn, *a, **kw) -> bool:
        try:
            fn(*a, **kw)
        except Kc02SafetyError:
            return True
        except Kc02Error:
            return False
        return False

    def raises_error(fn, *a, **kw) -> bool:
        """Any Kc02Error (safety or validation) counts."""
        try:
            fn(*a, **kw)
        except Kc02Error:
            return True
        return False

    def fields(cbw: bytes) -> tuple:
        assert len(cbw) == CBW_LENGTH, len(cbw)
        sig, tag, length, flags, lun, cblen = struct.unpack("<4sIIBBB", cbw[:15])
        cdb = cbw[15:31]
        assert cblen == 16 and len(cdb) == 16
        fn = struct.unpack("<I", cdb[1:5])[0]
        buf = struct.unpack("<I", cdb[5:9])[0]
        cb = struct.unpack("<I", cdb[9:13])[0]
        arg = cdb[13] | (cdb[14] << 8) | (cdb[15] << 16)
        return sig, tag, length, flags, lun, cdb[0], fn, buf, cb, arg

    # --- 1. legacy read CBW ------------------------------------------------
    sig, tag, length, flags, lun, op, fn, buf, cb, arg = fields(
        _build_read_cbw(0x11, 0x020C869C, 0x1000)
    )
    check(sig == CBW_SIGNATURE, "read CBW signature")
    check(tag == 0x11 and length == 0x1000 and lun == 0, "read CBW header fields")
    check(flags == READ_DIRECTION, "read CBW direction must be 0x80")
    check(op == VENDOR_OPCODE, "read CBW opcode")
    check(fn == MEMORY_READ_FN, "read CBW fn")
    check(buf == 0x020C869C, "read CBW buffer")
    check(cb == CALLBACK_SKIP, "read CBW callback must be skip")
    check(arg == 0, "read CBW callback arg")

    # --- 2. shim load CBW (write direction + cache-clean callback) ---------
    sig, tag, length, flags, lun, op, fn, buf, cb, arg = fields(
        _build_cbw(0x12, MEMORY_READ_FN, WRITE_DIRECTION, SHIM_LOAD_ADDR,
                   SHIM_LEN, CACHE_CLEAN_FN, 0)
    )
    check(flags == WRITE_DIRECTION, "load CBW direction must be 0x00")
    check(fn == MEMORY_READ_FN, "load CBW fn")
    check(buf == SHIM_LOAD_ADDR, "load CBW buffer is the shim scratch")
    check(length == SHIM_LEN, "load CBW length")
    check(cb == CACHE_CLEAN_FN, "load CBW callback is the D-cache cleaner")
    check(arg == 0, "load CBW callback arg")

    # --- 3. flash-read CBW --------------------------------------------------
    for off in (0x0, 0x0D3200, 0xFFFE00, 0xFFFFFF):
        sig, tag, length, flags, lun, op, fn, buf, cb, arg = fields(
            _build_cbw(0x13, MEMORY_READ_FN, READ_DIRECTION, DATA_BUFFER_ADDR,
                       SPI_CHUNK, SHIM_LOAD_ADDR, off)
        )
        check(flags == READ_DIRECTION, "flash-read direction")
        check(buf == DATA_BUFFER_ADDR, "flash-read buffer")
        check(length == SPI_CHUNK, "flash-read length")
        check(cb == SHIM_LOAD_ADDR, "flash-read callback is the shim")
        check(arg == off, f"flash-read 24-bit callback arg for 0x{off:X}")
    check(raises_error(_build_cbw, 1, MEMORY_READ_FN, READ_DIRECTION,
                       DATA_BUFFER_ADDR, SPI_CHUNK, SHIM_LOAD_ADDR, 0x1000000),
          "24-bit callback arg overflow must raise")

    # --- 4. forbidden / foreign function pointers ---------------------------
    for bad in sorted(FORBIDDEN_FUNCTIONS) + [0x0, 0x0203D38C, 0xDEADBEEF, 0x0204C625]:
        check(raises(_build_cbw, 1, bad, READ_DIRECTION, 0x020C869C, 16,
                     CALLBACK_SKIP, 0),
              f"foreign/forbidden fn 0x{bad:08X} must raise")
        check(raises(_require_read_only_call, bad, CALLBACK_SKIP),
              f"_require_read_only_call must reject fn 0x{bad:08X}")
    check(raises(_require_read_only_call, MEMORY_READ_FN, CACHE_CLEAN_FN),
          "_require_read_only_call must reject a non-skip callback")
    check(raises(_require_callback, CALLBACK_SKIP - 1),
          "_require_callback must reject a foreign callback")
    check(raises_error(_build_cbw, 1, MEMORY_READ_FN, 0x02, DATA_BUFFER_ADDR, 16,
                       CALLBACK_SKIP, 0),
          "unknown bmCBWFlags must raise")

    # --- 5. forbidden / foreign callbacks -----------------------------------
    for bad in sorted(FORBIDDEN_FUNCTIONS) + [0x0, 0x0203D38C, 0xDEADBEEF,
                                              0x020D8004, 0x020D8084]:
        check(raises(_build_cbw, 1, MEMORY_READ_FN, READ_DIRECTION,
                     DATA_BUFFER_ADDR, SPI_CHUNK, bad, 0),
              f"foreign/forbidden callback 0x{bad:08X} must raise")
    for good in sorted(ALLOWED_CALLBACKS):
        try:
            _build_cbw(1, MEMORY_READ_FN, READ_DIRECTION, DATA_BUFFER_ADDR,
                       SPI_CHUNK, good, 0)
        except Kc02Error as exc:
            check(False, f"allow-listed callback 0x{good:08X} rejected: {exc}")

    # --- 6. writes are hard-bounded to the scratch window -------------------
    outside = [
        0x00000000, 0x00008040, 0x00008250, 0x00044000, 0x01FFDA00,
        0x02000000, 0x02043FFF, 0x020D8000 - 4, 0x020D8080 - 4,
        0x020D8100 + DATA_BUFFER_LEN, 0x027FEC00, 0x027FF000, 0x80000000,
        0xFFFFFFFF,
    ]
    for bad in outside:
        check(raises(_require_scratch_write, bad, 16),
              f"write at 0x{bad:08X} must raise")
        check(raises(_build_cbw, 1, MEMORY_READ_FN, WRITE_DIRECTION, bad, 16,
                     CALLBACK_SKIP, 0),
              f"write CBW at 0x{bad:08X} must raise")
    # straddling a window boundary
    check(raises(_require_scratch_write, SHIM_LOAD_ADDR, SHIM_LEN + 4),
          "write straddling the shim window must raise")
    check(raises(_require_scratch_write, CANARY_LOAD_ADDR, CANARY_LEN + 4),
          "write straddling the canary window must raise")
    check(raises(_require_scratch_write, DATA_BUFFER_ADDR, DATA_BUFFER_LEN + 4),
          "write straddling the data buffer must raise")
    check(raises(_require_scratch_write, SHIM_LOAD_ADDR + SHIM_LEN, 4),
          "write starting just past the shim window must raise")
    check(raises(_require_scratch_write, SHIM_LOAD_ADDR, 0),
          "zero-length write must raise")
    # the designated windows are accepted
    for addr, length in ((SHIM_LOAD_ADDR, SHIM_LEN),
                         (CANARY_LOAD_ADDR, CANARY_LEN),
                         (DATA_BUFFER_ADDR, DATA_BUFFER_LEN)):
        try:
            _require_scratch_write(addr, length)
        except Kc02Error as exc:
            check(False, f"declared window 0x{addr:08X}+{length} rejected: {exc}")
    # load_scratch refuses a non-designated target even if it is in RAM
    check(raises(Kc02Device().load_scratch, 0x020C869C, b"\x00" * 4),
          "load_scratch must refuse a non-designated target")
    check(raises(Kc02Device().load_scratch, 0x00008040, b"\x00" * 4),
          "load_scratch must refuse the uncached low SRAM")
    check(raises(Kc02Device().load_scratch, SHIM_LOAD_ADDR, b"\x00" * (SHIM_LEN + 4)),
          "load_scratch must refuse an oversized shim blob")
    # and accepts its own windows (the closed device then trips its assert)
    for addr, blob in ((SHIM_LOAD_ADDR, SHIM_BYTES),
                       (CANARY_LOAD_ADDR, CANARY_BYTES),
                       (DATA_BUFFER_ADDR, b"\x00" * DATA_BUFFER_LEN)):
        try:
            Kc02Device().load_scratch(addr, blob)
            check(False, f"closed device should not transfer to 0x{addr:08X}")
        except Kc02SafetyError as exc:
            check(False, f"load_scratch rejected its own window 0x{addr:08X}: {exc}")
        except AssertionError:
            pass  # guards passed; only "device not open" fired

    # --- 7. scratch layout ------------------------------------------------
    check(SHIM_LOAD_ADDR + SHIM_LEN <= CANARY_LOAD_ADDR, "shim/canary overlap")
    check(CANARY_LOAD_ADDR + CANARY_LEN <= DATA_BUFFER_ADDR, "canary/buffer overlap")
    check(SPI_CHUNK % 16 == 0, "SPI chunk must be a multiple of 16")
    check(DATA_BUFFER_LEN == SPI_CHUNK, "data buffer must hold one SPI chunk")
    check(SPI_READ_FN not in FORBIDDEN_FUNCTIONS, "shim target must not be forbidden")
    check(CALLBACK_SKIP not in LOADABLE_ADDRS, "skip must not be a load target")
    check(all(not (lo <= CALLBACK_SKIP < hi) for lo, hi in WRITABLE_WINDOWS),
          "the 0xFFFFFFFF sentinel must not be a scratch address")
    check(FLASH_SIZE <= MAX_FLASH_OFFSET + 1, "flash size must fit the 24-bit arg")

    # --- 8. canary pattern --------------------------------------------------
    pat = _canary_pattern()
    check(len(pat) == DATA_BUFFER_LEN, "canary pattern length")
    check(_canary_pattern() == pat, "canary pattern must be deterministic")
    check(len(set(pat)) > 16, "canary pattern must not be trivially uniform")
    check(raises_error(Kc02Device().canary, b"\x00" * 8),
          "canary must reject a wrongly sized pattern")
    check(CALLBACK_SKIP in ALLOWED_CALLBACKS, "skip must remain allowed")

    # --- 9. poke guard: RAM only, no MMIO / flash / SPR ---------------------
    check(RAM_POKE_LO == 0x020C0000 and RAM_POKE_HI == 0x027FEC00,
          "poke window constants")
    check(RAM_WINDOW_LO <= RAM_POKE_LO and RAM_POKE_HI <= RAM_WINDOW_HI,
          "poke window inside the proven-writable RAM window")
    for bad in (0x00000000, 0x00008040, 0x00008250, 0x00008904, 0x0000A048,
                0x00044000, 0x01FFDA00, 0x02000000, 0x020BFFFF, 0x0203D584,
                0x02040000, 0x027FEFFC, 0x027FEC00, 0x20000002, 0x80000000,
                0xFFFFFFFF):
        check(raises(_require_poke_write, bad, 4),
              f"poke at 0x{bad:08X} must be refused")
    # the entire unlock patch target set is inside the window
    _fake_rec = bytearray(unlock.MAINMENU_REC_LEN)
    struct.pack_into("<I", _fake_rec, 0x08, unlock.MAINMENU_ENTER_FN)
    for label, addr, blob in unlock.plan(bytes(_fake_rec)):
        check(RAM_POKE_LO <= addr and addr + len(blob) <= RAM_POKE_HI,
              f"unlock write '{label}' inside the poke window")
        try:
            _require_poke_write(addr, len(blob))
        except Kc02Error as exc:
            check(False, f"unlock write '{label}' rejected by the guard: {exc}")
    # straddling / oversized / empty / unaligned
    check(raises(_require_poke_write, 0x020C0000, 0), "zero-length poke refused")
    check(raises(_require_poke_write, 0x020BFFFE, 4), "poke straddling the RAM floor")
    check(raises(_require_poke_write, 0x027FEBFE, 4), "poke straddling the RAM ceiling")
    # a poke that ends exactly on the ceiling is inside and accepted
    try:
        _require_poke_write(RAM_POKE_HI - 4, 4)
    except Kc02Error as exc:
        check(False, f"poke ending exactly on the ceiling rejected: {exc}")
    # a poke starting exactly on the ceiling is outside
    check(raises(_require_poke_write, RAM_POKE_HI, 4),
          "poke starting on the ceiling refused")
    check(raises(_require_poke_write, 0x020C0002, 4), "unaligned poke refused")
    check(raises(_require_poke_write, 0x020C0000, MAX_POKE_LEN + 4),
          "oversized poke refused")
    check(raises(Kc02Device().poke, 0x00008040, b"\x00\x00\x00\x00"),
          "poke() must refuse MMIO")
    check(raises(Kc02Device().poke, 0x02000000, b"\x00\x00\x00\x00"),
          "poke() must refuse flash/rodata")

    # --- 9b. code patch-site allowlist: exact match only --------------------
    check(RAM_POKE_LO > 0x0207B7FF,
          "the RAM floor must sit above the whole app code region")
    check(tuple(CODE_PATCH_SITES) == tuple(unlock.CODE_PATCH_SITES),
          "kc02_usb and kc02_unlock share one code-site allowlist")
    _sites = list(unlock.CODE_PATCH_SITES)
    check(len(_sites) == 4,
          "four code patch sites are enumerated (hoist + three child-lock)")
    _site, _site_len, _stock, _label = _sites[0]
    check((_site, _site_len) == (unlock.PER_FRAME_HOOK_ADDR, 4),
          "the first enumerated code site is the per-frame hook entry")
    check(_site < 0x0207B800, "the code site lies in the app code region")
    # the site itself is admitted (and is NOT silently admitted as RAM)
    try:
        _require_poke_write(_site, _site_len)
    except Kc02Error as exc:
        check(False, f"the enumerated code site was rejected: {exc}")
    check(not (RAM_POKE_LO <= _site < RAM_POKE_HI),
          "the code site is outside the RAM window (allowlisted on its own)")
    # every neighbouring / neighbouring-length variation is refused
    for _a, _l in ((_site, 1), (_site, 3), (_site, 5), (_site, 8), (_site, 0x34),
                   (_site - 4, 4), (_site + 4, 4), (_site + 0x30, 4),
                   (0x02000000, 4), (0x02000300, 4),
                   (0x02008188, 4), (0x020082A4, 4), (0x020082BC, 4),
                   (0x020130BC, 4), (0x0200D588, 4), (0x02006C74, 4),
                   (0x02004944, 4), (0x0203D38C, 4), (0x0203D584, 4),
                   (0x02002418, 4), (0x0207B7FC, 4)):
        check(raises(_require_poke_write, _a, _l),
              f"code-region poke 0x{_a:08X}+{_l} must be refused")

    # --- 9c. CHILD-LOCK: exact sites, guards, stub targets -------------------
    check(len(unlock.CHILDLOCK_CODE_SITES) == 3,
          "three child-lock code words are enumerated")
    for _i, (_s, _sl, _st, _lb) in enumerate(unlock.CHILDLOCK_CODE_SITES):
        check((_s, _sl) == (unlock.CHILDLOCK_WORD_SITES[_i][0], 4),
              f"child-lock site {_i} matches its plan entry")
        check(_st == unlock.CHILDLOCK_WORD_SITES[_i][2],
              f"child-lock site {_i} stock bytes match the plan entry")
        check(_s not in FORBIDDEN_FUNCTIONS,
              f"child-lock site 0x{_s:08X} is not a flash routine")
        check(_s < 0x0207B800, f"child-lock site 0x{_s:08X} is in the app code region")
        try:
            _require_poke_write(_s, 4)
        except Kc02Error as exc:
            check(False, f"child-lock site 0x{_s:08X} rejected by the guard: {exc}")
        for _a, _l in ((_s, 1), (_s, 3), (_s, 5), (_s, 8),
                       (_s - 4, 4), (_s + 4, 4), (_s + 0x28, 4)):
            check(raises(_require_poke_write, _a, _l),
                  f"child-lock code poke 0x{_a:08X}+{_l} must be refused")
    for _a, _l in ((0x02009F0C, 4), (0x02009894, 4), (0x020098A8, 4),
                   (0x020004B0, 4), (0x02009F04, 4), (0x02009F6C, 4),
                   (0x0200A4BC, 4), (0x02000494, 4), (0x02009E70, 4)):
        check(raises(_require_poke_write, _a, _l),
              f"neighbouring handler word 0x{_a:08X} must be refused")
    # every write of both child-lock plans passes the real guard
    for _plan_name, _plan in (("apply", unlock.childlock_plan()),
                              ("restore", unlock.childlock_restore_plan()),
                              ("live", unlock.childlock_live_plan()),
                              ("adult-on", unlock.childlock_adult_plan(True)),
                              ("adult-off", unlock.childlock_adult_plan(False))):
        for _lbl, _addr, _blob in _plan:
            try:
                _require_poke_write(_addr, len(_blob))
                _require_poke_payload(_blob)
            except Kc02Error as exc:
                check(False, f"child-lock {_plan_name} '{_lbl}' rejected: {exc}")
        check(all(struct.pack("<I", _f) not in _b
                  for _lbl, _addr, _b in _plan for _f in FORBIDDEN_FUNCTIONS),
              f"child-lock {_plan_name} embeds no forbidden routine address")
    # the two hijack words must point into the RAM window (where the stubs live)
    for _site2, _st2, _pt2, _lb2, _lbl2 in unlock.CHILDLOCK_WORD_SITES:
        if struct.unpack("<I", _pt2)[0] >> 26 != 0x00:
            continue          # the boot-mode word is an b.addi, not a hijack
        _tgt = unlock._decode_lj(_site2, _pt2)
        check(RAM_POKE_LO <= _tgt < RAM_POKE_HI,
              f"hijack 0x{_site2:08X} -> 0x{_tgt:08X} stays inside the RAM poke window")
    check(unlock._decode_lj(unlock.MENU_GATE_HANDLER_ADDR, unlock.MENU_GATE_HIJACK_BYTES)
          == unlock.GATE_STUB_ADDR, "menu-gate hijack decodes to the gate stub")
    check(unlock._decode_lj(unlock.UP_HANDLER_ADDR, unlock.UP_HIJACK_BYTES)
          == unlock.SEQ_STUB_ADDR, "seq-tap hijack decodes to the seq stub")
    # the stubs themselves are plain RAM writes inside the guard
    check(RAM_POKE_LO <= unlock.GATE_STUB_ADDR < RAM_POKE_HI
          and unlock.GATE_STUB_ADDR + unlock.GATE_STUB_LEN <= unlock.SEQ_STUB_ADDR,
          "gate stub is RAM and does not overlap the seq stub")
    check(unlock.CHILDLOCK_STAGING_HI <= 0x020D9490,
          "child-lock staging is a small bounded RAM block")
    for _a, _l, _lb in ((unlock.GATE_STUB_ADDR, unlock.GATE_STUB_LEN, "gate stub"),
                        (unlock.SEQ_STUB_ADDR, unlock.SEQ_STUB_LEN, "seq stub"),
                        (unlock.CHILDLOCK_BYTES_ADDR, 4, "childlock bytes")):
        check(not (unlock.UNLOCK_BASE <= _a and _a < unlock.HOIST_STAGING_HI),
              f"the {_lb} is clear of the unlock/hoist staging")
        check(not (_a + _l > unlock.SHIM_SCRATCH_LO and _a < unlock.SHIM_SCRATCH_HI),
              f"the {_lb} is clear of the shim scratch")
    # ... while the hoist's RAM stub address goes through the ordinary RAM guard
    check(not raises(_require_poke_write, unlock.HOIST_STUB_ADDR,
                     unlock.HOIST_STUB_LEN),
          "the hoist RAM stub is admitted by the RAM window, not the code allowlist")
    # the hoist plan itself must pass the guard, in both halves
    for _label2, _addr2, _blob2 in unlock.hoist_plan():
        try:
            _require_poke_write(_addr2, len(_blob2))
            _require_poke_payload(_blob2)
        except Kc02Error as exc:
            check(False, f"hoist write '{_label2}' rejected by the guard: {exc}")
    # forbidden routines stay unreachable: no hoist byte may encode them
    for _lbl3, _a3, _b3 in unlock.hoist_plan():
        check(not any(
            struct.pack("<I", f) in _b3 for f in FORBIDDEN_FUNCTIONS
        ), f"hoist write '{_lbl3}' embeds no forbidden routine address")
    # the hoist must not point at any flash-modifying routine
    check(unlock.BOT_SERVICE_FN not in FORBIDDEN_FUNCTIONS,
          "the hoisted BOT service is not a flash routine")
    check(unlock.BOT_SERVICE_FN != unlock.BOT_WRAPPER_FN,
          "the hoist uses the pure service, not the panel-pumping wrapper")
    check(unlock.PER_FRAME_HOOK_ADDR not in FORBIDDEN_FUNCTIONS,
          "the patched hook is not a flash routine")
    check(all(s not in FORBIDDEN_FUNCTIONS for s, _l, _b, _lb in CODE_PATCH_SITES),
          "no enumerated code site is a flash routine")
    check(raises(Kc02Device().poke, 0x020CCD50, b""),
          "poke() must refuse an empty payload")
    check(raises(Kc02Device().poke, 0x020CCD50, b"\x00\x00"),
          "poke() must refuse an unaligned length")
    check(raises(Kc02Device().poke, 0x020CCD51, b"\x00\x00\x00\x00"),
          "poke() must refuse an unaligned address")
    check(set(unlock.FORBIDDEN_FUNCTIONS) == set(FORBIDDEN_FUNCTIONS),
          "kc02_unlock and kc02_usb agree on the forbidden routines")
    for bad in sorted(FORBIDDEN_FUNCTIONS):
        check(raises(_require_poke_payload, struct.pack("<I", bad)),
              f"poke payload embedding 0x{bad:08X} must be refused")
    check(not raises(_require_poke_payload, bytes(8)),
          "an ordinary poke payload is accepted")
    check(raises(Kc02Device().load_scratch, unlock.REC_ADDR, b"\x00" * 4),
          "load_scratch must still refuse a non-designated address")
    check(unlock.REC_ADDR + unlock.MAINMENU_REC_LEN <= RAM_POKE_HI,
          "unlock staging fits the poke window")
    import io as _io
    _buf = _io.StringIO()
    old_out, sys.stdout = sys.stdout, _buf
    try:
        unlock_rc = unlock.selftest()
    finally:
        sys.stdout = old_out
    check(unlock_rc == 0, "kc02_unlock selftest")

    # --- 10. unlock command wiring ----------------------------------------
    parser = build_parser()
    ns = parser.parse_args(["poke", "--addr", "0x020CCD50", "--hex", "06 00 00 00",
                            "--dry-run"])
    check(ns.func is _cmd_poke and ns.addr == 0x020CCD50,
          "poke subcommand wiring")
    ns2 = parser.parse_args(["unlock-usb", "--dry-run"])
    check(ns2.func is _cmd_unlock_usb and ns2.dry_run, "unlock-usb subcommand wiring")
    check(ns2.hoist is False, "unlock-usb defaults to no hoist")
    ns2h = parser.parse_args(["unlock-usb", "--dry-run", "--hoist"])
    check(ns2h.func is _cmd_unlock_usb and ns2h.hoist,
          "unlock-usb --hoist wiring")
    ns_h = parser.parse_args(["usb-hoist", "--dry-run"])
    check(ns_h.func is _cmd_usb_hoist and ns_h.dry_run, "usb-hoist subcommand wiring")
    ns_cl = parser.parse_args(["child-lock", "--dry-run"])
    check(ns_cl.func is _cmd_child_lock and ns_cl.action == "apply" and ns_cl.dry_run,
          "child-lock defaults to apply + --dry-run wiring")
    ns_clr = parser.parse_args(["child-lock", "--restore", "--dry-run"])
    check(ns_clr.func is _cmd_child_lock and ns_clr.restore, "child-lock --restore wiring")
    ns_cla = parser.parse_args(["child-lock", "adult-on"])
    check(ns_cla.func is _cmd_child_lock and ns_cla.action == "adult-on",
          "child-lock adult-on wiring")
    ns_clb = parser.parse_args(["child-lock", "adult-off", "--dry-run"])
    check(ns_clb.action == "adult-off" and ns_clb.dry_run, "child-lock adult-off wiring")
    check(_parse_poke_payload(ns) == b"\x06\x00\x00\x00", "poke --hex parsing")
    ns3 = parser.parse_args(["poke", "--addr", "0x200", "--bytes", "6,0,0,0"])
    check(_parse_poke_payload(ns3) == b"\x06\x00\x00\x00", "poke --bytes parsing")
    ns_bad = parser.parse_args(["poke", "--addr", "0x200", "--hex", "zz"])
    check(raises_error(_parse_poke_payload, ns_bad), "poke must reject bad hex")
    ns_none = parser.parse_args(["poke", "--addr", "0x200"])
    check(raises_error(_parse_poke_payload, ns_none),
          "poke must require exactly one payload source")
    ns_two = parser.parse_args(["poke", "--addr", "0x200", "--hex", "00",
                               "--bytes", "0"])
    check(raises_error(_parse_poke_payload, ns_two),
          "poke must reject two payload sources")
    check(raises_error(_parse_poke_payload,
                       parser.parse_args(["poke", "--addr", "0x200",
                                          "--bytes", "256"])),
          "poke --bytes must reject out-of-range values")

    if failures:
        print("SELFTEST FAILED:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    print("kc02_usb selftest OK (no device was opened):")
    print(f"  fn slot pinned to 0x{MEMORY_READ_FN:08X}")
    print("  callback slots allowed: "
          + ", ".join(f"0x{c:08X}" for c in sorted(ALLOWED_CALLBACKS)))
    print("  writable windows: "
          + ", ".join(f"0x{lo:08X}..0x{hi:08X}" for lo, hi in WRITABLE_WINDOWS))
    print(f"  forbidden routines unreachable: "
          + ", ".join(f"0x{f:08X}" for f in sorted(FORBIDDEN_FUNCTIONS)))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="kc02_usb.py",
        description="CPU-memory reader and SPI-flash reader for the HiMont KC02 "
                    "camera. Never writes or erases flash.",
    )
    parser.add_argument("--vid", type=_parse_int, default=DEFAULT_VID,
                        help="USB vendor id (default: 0x0219)")
    parser.add_argument("--pid", type=_parse_int, default=DEFAULT_PID,
                        help="USB product id (default: 0x3280)")
    parser.add_argument("--timeout-ms", type=int, default=DEFAULT_TIMEOUT_MS,
                        help=f"USB transfer timeout in ms (default: "
                             f"{DEFAULT_TIMEOUT_MS})")

    sub = parser.add_subparsers(dest="command", required=True)

    p_read = sub.add_parser("read", help="read CPU memory")
    p_read.add_argument("--addr", type=_parse_int, required=True,
                        help="start address, decimal or 0x-prefixed hex")
    p_read.add_argument("--len", dest="length", type=_parse_int, required=True,
                        help="byte count, decimal or 0x-prefixed hex")
    p_read.add_argument("--out", default=None,
                        help="write raw bytes to this file instead of hexdump")
    p_read.set_defaults(func=_cmd_read)

    p_probe = sub.add_parser("probe", help="flash-window test: 'BLDR' at 0x01FFDA04")
    p_probe.set_defaults(func=_cmd_probe)

    p_dump = sub.add_parser("dump-ram", help="dump the known RAM window")
    p_dump.add_argument("--out", required=True, help="output file for the RAM dump")
    p_dump.set_defaults(func=_cmd_dump_ram)

    p_info = sub.add_parser("shim-info",
                            help="offline: print the shim/canary bytes and guards")
    p_info.set_defaults(func=_cmd_shim_info)

    p_self = sub.add_parser("selftest",
                            help="offline: verify CBW fields, guards and bounds")
    p_self.set_defaults(func=_cmd_selftest)

    p_load = sub.add_parser(
        "load-shim",
        help="write the SPI-read shim + canary into fixed RAM scratch and "
             "D-cache-clean them (does not touch flash)",
    )
    p_load.set_defaults(func=_cmd_load_shim)

    p_can = sub.add_parser(
        "canary",
        help="prove RAM execution with an l.jr r9 stub before trusting the shim",
    )
    p_can.set_defaults(func=_cmd_canary)

    p_flash = sub.add_parser(
        "flash-read",
        help="read SPI flash through the RAM shim (flash is never written)",
    )
    p_flash.add_argument("--offset", type=_parse_int, required=True,
                         help="24-bit flash byte offset, decimal or 0x-prefixed hex")
    p_flash.add_argument("--len", dest="length", type=_parse_int, required=True,
                         help="byte count, decimal or 0x-prefixed hex")
    p_flash.add_argument("--out", default=None,
                         help="write raw bytes to this file instead of hexdump")
    p_flash.add_argument("--quiet", action="store_true",
                         help="suppress the progress meter")
    p_flash.set_defaults(func=_cmd_flash_read)

    p_poke = sub.add_parser(
        "poke",
        help="write bytes to writable app RAM (0x020c0000..0x027FEC00 only) "
             "and read them back; refuses flash/MMIO/SPR addresses",
    )
    p_poke.add_argument("--addr", type=_parse_int, required=True,
                        help="CPU address, decimal or 0x-prefixed hex")
    p_poke.add_argument("--hex", dest="hex_bytes", default=None,
                        help="hex bytes, e.g. '00 48 00 44' or '00480044'")
    p_poke.add_argument("--bytes", dest="bytes_list", default=None,
                        help="decimal/hex byte list, e.g. '0,72,0,68'")
    p_poke.add_argument("--from-file", dest="from_file", default=None,
                        help="read the payload from this file")
    p_poke.add_argument("--no-verify", action="store_true",
                        help="skip the read-back check (not recommended)")
    p_poke.add_argument("--dry-run", action="store_true",
                        help="validate and print the guarded write, send nothing")
    p_poke.set_defaults(func=_cmd_poke)

    p_unlock = sub.add_parser(
        "unlock-usb",
        help="apply the RAM-only patch that disables the USB lock screen while "
             "keeping the USB/MSC (0xCD) transport alive",
    )
    p_unlock.add_argument("--dry-run", action="store_true",
                          help="read the device, print the exact writes, then "
                               "send nothing")
    p_unlock.add_argument("--rec", default=None,
                          help="offline: build the patch from a saved 0x100-byte "
                               "stock Main-menu record file instead of the device")
    p_unlock.add_argument("--hoist", action="store_true",
                          help="also apply the per-frame USB/BOT hoist (usb-hoist) "
                               "so the transport is serviced in EVERY UI mode")
    p_unlock.set_defaults(func=_cmd_unlock_usb)

    p_hoist = sub.add_parser(
        "usb-hoist",
        help="hoist the USB/BOT service out of UI mode 10 into every UI mode: "
             "a 36-byte RAM trampoline plus ONE allowlisted code word at "
             "0x02000404 (the every-mode per-frame hook FUN_02000404)",
    )
    p_hoist.add_argument("--dry-run", action="store_true",
                         help="validate the guards and print the exact writes, "
                              "then send nothing")
    p_hoist.set_defaults(func=_cmd_usb_hoist)

    p_cl = sub.add_parser(
        "child-lock",
        help="child-friendly lock: boot straight into the camera (mode 3) and "
             "gate the Main menu behind an adult action (tap UP, then POWER). "
             "Two RAM stubs + two RAM bytes + THREE allowlisted code words.",
    )
    p_cl.add_argument("action", nargs="?",
                      choices=["apply", "adult-on", "adult-off"],
                      default="apply",
                      help="apply the lock (default), or just set/clear the "
                           "adult override byte")
    p_cl.add_argument("--restore", action="store_true",
                      help="write the three stock code words back and zero the "
                           "RAM stubs/bytes")
    p_cl.add_argument("--dry-run", action="store_true",
                      help="validate the guards and print the exact writes, "
                           "then send nothing")
    p_cl.set_defaults(func=_cmd_child_lock)

    return parser


def main(argv: Optional[list] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        return args.func(args)
    except Kc02Error as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
