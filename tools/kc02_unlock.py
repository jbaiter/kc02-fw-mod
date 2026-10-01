#!/usr/bin/env python3
"""kc02_unlock.py - RAM-only "disable the USB lock screen" patch for the KC02.

WHAT THE LOCK IS (static RE, see HANDOVER_KC02_CODE_RE.md)
---------------------------------------------------------
The KC02 has a small application-mode machine (FUN_02008188) driven by a table
of 12 mode records (DAT_020ccd5c).  Mode 10 is the record at 0x020c339c, named
"usb device" (string 0x0207ba8b):

    +0x00 name  = 0x0207ba8b  "usb device"
    +0x04 arg
    +0x08 enter = 0x0200d498   (renders UI state 7 = the SD-card/USB screen,
                                re-selects the USB mode, installs the mode-10
                                key-hook list 0x020c3354)
    +0x0c exit  = 0x0200d450   (FUN_0204a9a4 -> FUN_0202ad98(0xf,0): USB off)
    +0x10 frame = 0x0200d588   (polls the BOT transport FUN_0204abc8)

When the host starts talking BOT, the USB state machine FUN_0200c8d0 moves
0x020d416e 1->2 and posts event type 2; the hook FUN_02007ee4 then requests
app mode 10 (FUN_02008108(10,1)).  Mode 10 has *no camera UI and no working key
handlers* - the hook list it installs (0x020c3354) maps every button to a
no-op stub - so the camera is unusable until USB is unplugged.

The BOT/SCSI processor FUN_0204d0a8 is reachable through exactly one path:
    FUN_0204abc8 -> FUN_0204d0a8,  and FUN_0204abc8 is called only from
    mode 10's frame function 0x0200d588.
So the USB transport cannot simply be "left alone while we render some other
mode": whatever mode runs has to keep calling FUN_0204abc8.

THE PATCH
---------
All firmware code and the mode records live in flash (XIP, read-only to this
transport).  What *is* writable RAM is the mode table itself (DAT_020ccd5c,
0x020ccd5c) plus the mode bookkeeping pointers, and the proven RAM window
0x020D8000+.  So the patch rewrites mode 10 to point at a RAM copy of the
Main-menu record whose frame function is a tiny RAM stub:

    +0x0c exit  = 0                    (never disables the UI/USB on the way out)
    +0x10 frame = RAM stub, which does FUN_0204abc8() (service the BOT transport)
                  and then the stock Main-menu frame 0x020130bc

Everything else in the record (name, arg, enter, the mode-6 key-hook table, the
two data pointers at +0x14/+0x18) is a byte-for-byte copy of the stock Main-menu
record, so no stock code can tell the difference.  Result:

  * the display shows the normal camera UI instead of the SD-card screen,
  * buttons work (the Main-menu hook table is installed by the stock enter),
  * the 0xCD / BOT mass-storage transport keeps being serviced every frame,
  * the USB peripheral is never disabled (we never run 0x0200d450),
  * re-entry is harmless: mode 10 *is* the patched record from now on, so the
    firmware cannot re-lock no matter how often event type 2 fires.

The patch is five bounded RAM writes (stub, record, mode-table slot, active-rec
pointer, pending-mode).  No flash access, no code patch, fully reverted by a
power cycle.

TASK A: THE USB/BOT HOIST (``hoist_plan`` / ``usb-hoist``)
--------------------------------------------------------
The unlock above fixes exactly one mode (10).  It still leaves the transport
dead whenever the user is in any *other* UI mode, because stock firmware
services BOT only from mode 10's frame.  The hoist removes that coupling:

    FUN_0200046c
      0x02000528 l.jal FUN_02008188        mode machine / mode-table walker
      FUN_02008188
        0x020082a4 l.jal FUN_02000404       PER-FRAME HOOK - every mode,
                                            every frame, unconditional
        0x020082bc l.jalr *(rec+0x10)       the mode's own frame function

FUN_02000404 is the single function that runs once per UI frame whatever the
mode or UI state is, and it has exactly one caller (0x020082a4).  The hoist
replaces its first instruction - ``b.sw -0x4(r1),r9`` - with
``l.j 0x020D9200``, pointing at a RAM trampoline that calls the pure BOT
service ``FUN_0204d0a8`` (poll -> parse -> dispatch), then re-executes the
instruction it displaced and jumps back into the stock body at 0x02000408.

``l.j`` is used rather than ``l.jal`` precisely because it does *not* touch r9:
the hook body saves r9 at entry-4 and its own epilogue restores it, so the
walker still returns to 0x020082a8.  The trampoline spills/restores both r9 and
r2 (r2 is callee-saved and the walker keeps DAT_020ccd50 live in it), using the
firmware's own save-first idiom - stores at negative offsets of the entry r1
*before* ``b.addi r1,r1,-N``.

The hoist calls ``FUN_0204d0a8`` and not ``FUN_0204abc8``: the wrapper also
pumps the cosmetic panel/JPEG feed ``FUN_0204a9fc``, which should only run
where stock ran it.  Composable with the mode-10 bypass above - the two touch
disjoint addresses and may be applied in either order (``unlock-usb --hoist``
applies both).  The unlock's frame stub no longer *needs* its ``0x0204abc8``
call once the hoist is in, but both paths keep working.

TASK B: THE CHILD LOCK (``childlock_plan`` / ``child-lock``)
-----------------------------------------------------------
Two goals, three code words and two RAM trampolines:

1. **Boot straight into the camera.**  ``ui_app_task`` selects the initial mode
   at ``0x02000494..0x020004B4`` with ``(usb_sense_state == 2) ? 10 : 6``.  Only
   the else-branch immediate changes, 6 -> 3 (camera), so the firmware's own
   display: none of the mode machine, the mode table or the boot sequence is
   otherwise touched.  The mode-10 branch (USB already driving BOT at power-on)
   is left alone; ``unlock-usb``/``usb-hoist`` cover that case.

2. **Menu reachable only through an adult key action.**  In the camera the
   POWER short press is event ``0x24`` -> ``key_mode3_power_request_menu``
   ``0x02009f08`` -> ``ui_mode_request(6)``.  Its entry word is hijacked to a
   RAM gate stub.  The adult action is a **two-tap sequence: UP, then POWER**.
   A second hijack on the mode-3 UP handler (event ``0x1b``, ``0x02009890``,
   taken from the ``(id, handler)`` pairs in the mode-3 hook list ``0x020c2b50``)
   arms a RAM byte; the gate consumes that byte and lets exactly one POWER event
   through.  ``child-lock adult-on`` sets a second RAM byte that bypasses the
   sequence entirely (for an adult who does not want to tap twice).

Why a two-tap sequence and not a long press: ``key_event_dispatch`` hands every
hook the same ``(r3=payload, r4=1, r5=&ev)`` triple, so a handler cannot tell a
short from a long press itself, and the earlier analysis of the dispatch loop
concluded that payload-carrying key events never reach the hooks.  Later
listing work on 0x020079B4 (see docs/UI_STATE_MACHINE.md §4.2) showed that
conclusion is WRONG - every event with id <= 0x2d reaches both hook tables -
but the settled two-tap design is implemented as specified, and it works
independently of that question (a second tap is a strictly stronger signal than
a payload value, and it needs no dispatch-level assumption).

Both hijack words are ``l.j``, never ``l.jal``: ``l.j`` sets no link register,
so r9 still holds the caller's return address and the gate's fail path can be a
bare ``l.jr r9`` (no stack traffic at all).  The stubs only use r6/r7, which are
scratch-safe across calls here, so r3/r4/r5 reach the stock handler intact.

Nothing in this patch can reach flash: the two stubs and the two flag bytes are
ordinary RAM writes, and the three code words are admitted one-by-one by the
exact (address, length) allowlist in ``CODE_PATCH_SITES``.

SAFETY
------
This module only *describes* patches.  It never talks to a device.  Every
address in ``plan()``/``hoist_plan()`` is asserted to be either inside the RAM
window 0x020c0000..0x027FEC00 (clear of the shim/canary/data-buffer scratch) or
the single enumerated code patch site in ``CODE_PATCH_SITES``.  Nothing here
can reach a flash erase/program routine.

The hoist (TASK A) writes exactly four bytes of code - the first instruction of
the per-frame hook FUN_02000404 at 0x02000404 - replaced by a ``l.j`` to a RAM
trampoline.  That address is admitted by an exact (address, length) allowlist in
``CODE_PATCH_SITES``; kc02_usb._require_poke_write uses it and refuses every
other code address.  There is no widened range.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from typing import List, Sequence, Tuple

# Encoders/encodings are shared with the Ghidra-verified shim module.
try:
    from kc02_shim import (
        ENC_ADD_SP_N8,
        ENC_ADD_SP_P8,
        ENC_JR_R9,
        ENC_LWZ_M4_R9,
        ENC_LWZ_M8_R2,
        ENC_SW_M4_R9,
        ENC_SW_M8_R2,
        enc_jal,
        enc_lj,
        enc_ori,
    )
except ImportError as exc:  # pragma: no cover - only when run out of tree
    raise SystemExit(
        "kc02_shim.py must sit next to kc02_unlock.py (run as "
        "'python3 tools/kc02_unlock.py ...'): " + str(exc)
    )

import kc02_shim as shim  # module handle for the shared self-test

# --------------------------------------------------------------------------
# Firmware addresses (all CONFIRMED from disassembly, see module docstring)
# --------------------------------------------------------------------------
MODE_TABLE_ADDR = 0x020CCD5C      # DAT_020ccd5c: 12 u32 mode-record pointers
MODE_TABLE_COUNT = 12
MODE_PENDING_ADDR = 0x020CCD50    # DAT_020ccd50: requested mode (0xc = idle, 0xb = quit)
MODE_CURRENT_ADDR = 0x020CCD54    # DAT_020ccd54: current mode
MODE_ACTIVE_REC_ADDR = 0x020CCD58  # DAT_020ccd58: active mode record pointer
MODE_PREV_ADDR = 0x020CCD4C       # DAT_020ccd4c: previous mode (set by FUN_02008108)

# Flash-modifying routines; none of the patch bytes may embed these addresses
# (kc02_usb.py enforces the same rule for every poke).
FORBIDDEN_FUNCTIONS = frozenset(
    {
        0x0203D584,  # 4 KB SECTOR ERASE
        0x0203D454,  # PAGE PROGRAM
        0x0203D4E8,  # PAGE PROGRAM
        0x0203D25C,  # WREN (write-enable)
        0x02002418,  # DestBin.bin upgrade task (erase + program)
    }
)

USBMODE_INDEX = 10                # the "usb device" lock screen
MODE_TABLE_SLOT = MODE_TABLE_ADDR + 4 * USBMODE_INDEX

MAINMENU_REC_ADDR = 0x020C4C18    # stock mode-6 ("Main menu") record
MAINMENU_REC_LEN = 0x100          # covers name/arg/enter/exit/frame + key table
MAINMENU_FRAME_FN = 0x020130BC    # stock mode-6 per-frame function
MAINMENU_ENTER_FN = 0x020130E4    # stock mode-6 enter function

BOT_POLL_FN = 0x0204ABC8          # server the BOT/MSC transport (only path to 0x0204d0a8)

REC_OFF_EXIT = 0x0C
REC_OFF_FRAME = 0x10

# --------------------------------------------------------------------------
# TASK A: hoist the USB/BOT service out of UI "mode 10" into EVERY UI mode
# --------------------------------------------------------------------------
#
# Call chain (CONFIRMED from listings, see docs/UI_STATE_MACHINE.md):
#
#   FUN_0200046c            app task: boot init, pick the initial mode, then
#     0x02000528  l.jal ->  FUN_02008188      the mode machine / mode-table walker
#     FUN_02008188
#       0x020082a4  l.jal -> FUN_02000404     PER-FRAME HOOK: one call per UI
#                                            frame in EVERY mode, unconditional
#       0x020082bc  l.jalr -> *(active_rec+0x10)   the mode's own frame function
#
#   FUN_02000404 (0x02000404, 52 bytes) is the single function that runs every
#   frame regardless of mode or UI state.  It has exactly ONE call site
#   (0x020082a4).  Its first instruction is the standard `b.sw -0x4(r1),r9`.
#
#   The BOT service FUN_0204d0a8 (poll -> parse -> dispatch) is stock-reachable
#   only from FUN_0204abc8, whose only caller is mode 10's frame 0x0200d598.
#   FUN_0204abc8 additionally pumps the cosmetic panel/JPEG feed FUN_0204a9fc,
#   which is why this hoist calls the *pure* 0x0204d0a8 and not 0x0204abc8.
#
# The hoist is therefore a 2-part, RAM+one-word-code patch:
#
#   * HOIST_STUB_ADDR (RAM scratch, 36 B):
#         b.sw  -0x8(r1),r2      } the firmware's own save-first prologue idiom
#         b.sw  -0x4(r1),r9      }   (stores at negative offsets of the ENTRY r1,
#         b.addi r1,r1,-0x8      }    BEFORE the stack pointer is adjusted)
#         l.jal  0x0204d0a8        <- service the BOT/MSC transport
#         b.addi r1,r1,0x8
#         b.lwz  r2,-0x8(r1)     } restore exactly what the hook body needs
#         b.lwz  r9,-0x4(r1)     } (r2 is callee-saved and the walker keeps
#         b.sw  -0x4(r1),r9      }   DAT_020ccd50 live in r2 across the call)
#         l.j    0x02000408        <- resume the stock hook body, r9 intact
#
#   * PER_FRAME_HOOK_ADDR (flash/XIP code): its first instruction
#         b.sw -0x4(r1),r9      ->   l.j  <HOIST_STUB_ADDR>
#     `l.j` (not `l.jal`) leaves r9 = the walker's return address untouched, so
#     the stub can spill it, and the stock epilogue at 0x0200042c..0x02000434
#     (`addi r1,r1,8; lwz r9,-4(r1); lwz r1,-8(r1); jr r9`) still returns to
#     0x020082a8.  The instruction we overwrite is re-executed by the stub, so
#     entry-4 and entry-8 end up holding exactly what stock would have left.
#
# Only 4 bytes of code are ever written, and only at this one enumerated site.
PER_FRAME_HOOK_ADDR = 0x02000404          # FUN_02000404 entry (the every-mode frame hook)
PER_FRAME_HOOK_LEN = 0x34                 # 52-byte body, 0x02000404..0x02000437
PER_FRAME_HOOK_RESUME = 0x02000408        # instruction after the one we overwrite
PER_FRAME_HOOK_STOCK = bytes.fromhex("fc4fe1d7")  # b.sw -0x4(r1),r9
BOT_SERVICE_FN = 0x0204D0A8               # poll -> parse -> dispatch (pure BOT service)
BOT_WRAPPER_FN = 0x0204ABC8               # BOT service + panel/JPEG feed (mode 10's stock call)

# --------------------------------------------------------------------------
# TASK B: child-friendly lock - boot straight into the camera, and make the
# Main menu reachable only through an adult key action
# --------------------------------------------------------------------------
#
# (1) BOOT-TO-CAMERA - one code word, no stub.
#     ui_app_task picks the initial mode at 0x02000494..0x020004B4:
#         0x02000494  b.lbz  r3,0x2(r2)          r2 = 0x020d416c
#         0x02000498  b.sfnei r3,0x2             usb_sense_state == 2 ?
#         0x0200049C  b.bf   0x020004ac          (branch if flag SET)
#         0x020004A0  b.addi r3,r0,0xa           yes -> mode 10 (USB lock)
#         0x020004A4  b.addi r4,r0,0x1
#         0x020004A8  l.j    0x020004b4
#         0x020004AC  b.addi r3,r0,0x6           no  -> mode 6 (Main menu)
#         0x020004B0  b.addi r4,r0,0x0
#         0x020004B4  l.jal  0x02008108          ui_mode_request(r3, r4)
#     CONFIRMED (Ghidra listing of 0x02000494-0x020004B8).  Only the else-branch
#     immediate changes, 6 -> 3 (camera).  The USB-active-at-boot branch (mode
#     10) is left exactly as stock: `unlock-usb` / `usb-hoist` cover that case.
BOOT_MODE_WORD_ADDR = 0x020004AC
BOOT_MODE_WORD_STOCK = bytes.fromhex("0600609c")   # b.addi r3,r0,0x6  (Main menu)
BOOT_MODE_WORD_PATCH = bytes.fromhex("0300609c")   # b.addi r3,r0,0x3  (camera)

# (2) ADULT GATE ON THE MENU REQUEST - one code word + a RAM gate stub.
#     Camera (mode 3) POWER short press is event 0x24 -> the mode-3 hook list
#     binds it to key_mode3_power_request_menu 0x02009F08, whose body calls
#     ui_mode_request(6) at 0x02009F30.  Its entry instruction is the stock
#     `b.sw -0x4(r1),r9` (fc4fe1d7); we replace it with `l.j <gate stub>` and
#     let the stub re-execute that instruction when it decides to let the event
#     through.  `l.j` (not `l.jal`) keeps r9 pointing at key_event_dispatch, so
#     the stub can return with a bare `l.jr r9` and never touches the stack.
MENU_GATE_HANDLER_ADDR = 0x02009F08       # key_mode3_power_request_menu (event 0x24)
MENU_GATE_HANDLER_STOCK = bytes.fromhex("fc4fe1d7")   # b.sw -0x4(r1),r9
MENU_GATE_HANDLER_RESUME = 0x02009F0C      # second prologue store (r1 <- -0xc(r1))

# (3) THE ADULT KEY ACTION - one code word + a RAM tap stub.
#     key_event_dispatch (0x020079B4) hands every hook the SAME (r3=payload,
#     r4=1, r5=&ev) triple and a distinct payload is the only per-event
#     information a handler gets (see the module docstring: the long-press
#     design was dropped, so the adult action is a two-TAP sequence).  The
#     mode-3 hook list at 0x020C2B50 binds event 0x1b (UP) to 0x02009890; its
#     entry instruction is `b.sw -0xc(r1),r18` (f497e1d7).  Tapping UP arms
#     CHILDLOCK_SEQ; the next POWER press consumes it and reaches the menu.
UP_HANDLER_ADDR = 0x02009890              # mode-3 hook list entry for id 0x1b (UP)
UP_HANDLER_STOCK = bytes.fromhex("f497e1d7")          # b.sw -0xc(r1),r18
UP_HANDLER_RESUME = 0x02009894
MODE3_HOOK_LIST = 0x020C2B50              # (id, handler) pairs; 0x020C2B68 = (0x1b, 0x02009890)

# EXACT enumerated allowlist of code-region patch sites (addr, length,
# expected stock bytes, label).  kc02_usb._require_poke_write admits a write
# into the code region ONLY when (addr, length) matches one of these exactly -
# there is deliberately no widening of any range.
#
# The first entry is the per-frame USB/BOT hoist; the other three belong to the
# child-lock patch (TASK B, see below).
CODE_PATCH_SITES = (
    (
        PER_FRAME_HOOK_ADDR,
        4,
        PER_FRAME_HOOK_STOCK,
        "FUN_02000404 entry[0] -> l.j hoist stub (per-frame BOT service)",
    ),
    (
        BOOT_MODE_WORD_ADDR,
        4,
        BOOT_MODE_WORD_STOCK,
        "ui_app_task initial-mode select (else branch) 6 -> 3",
    ),
    (
        MENU_GATE_HANDLER_ADDR,
        4,
        MENU_GATE_HANDLER_STOCK,
        "key_mode3_power_request_menu entry -> l.j adult gate stub",
    ),
    (
        UP_HANDLER_ADDR,
        4,
        UP_HANDLER_STOCK,
        "mode-3 UP handler (hook list id 0x1b) entry -> l.j seq-tap stub",
    ),
)

# The subset the child-lock patch itself writes (order = write order in the plan).
CHILDLOCK_CODE_SITES = CODE_PATCH_SITES[1:]


def is_code_patch_site(address: int, length: int) -> bool:
    """True only for an EXACT (address, length) match of an allowed code patch site."""
    return any(address == site and length == site_len
               for site, site_len, _stock, _label in CODE_PATCH_SITES)


def expected_code_site_bytes() -> dict:
    """{address: stock bytes} for every enumerated code patch site."""
    return {site: stock for site, _l, stock, _lbl in CODE_PATCH_SITES}

# --------------------------------------------------------------------------
# RAM staging areas (inside 0x020D8000+, clear of the shim scratch 0x020D8000..0x020D82FF)
# --------------------------------------------------------------------------
UNLOCK_BASE = 0x020D9000
FRAME_STUB_ADDR = UNLOCK_BASE             # 36 bytes -> 0x020D9023
FRAME_STUB_LEN = 36
REC_ADDR = 0x020D9100                     # 0x100 bytes -> 0x020D91FF
UNLOCK_STAGING_HI = 0x020D9200            # exclusive end of the unlock staging

# The USB/BOT hoist (TASK A) lives immediately after the unlock staging.
HOIST_STUB_ADDR = 0x020D9200              # 36 bytes -> 0x020D9223
HOIST_STUB_LEN = 36
HOIST_STAGING_HI = HOIST_STUB_ADDR + HOIST_STUB_LEN

# The child-lock (TASK B) staging lives clear of the unlock/hoist staging.
GATE_STUB_ADDR = 0x020D9400               # 52 bytes -> 0x020D9433
GATE_STUB_LEN = 52
SEQ_STUB_ADDR = 0x020D9440                # 24 bytes -> 0x020D9457
SEQ_STUB_LEN = 24
CHILDLOCK_BYTES_ADDR = 0x020D9480         # aligned word holding the two flag bytes
CHILDLOCK_SEQ_ADDR = 0x020D9480           # byte 0: "a UP tap just happened" (one shot)
CHILDLOCK_FLAG_ADDR = 0x020D9481          # byte 1: adult override (set by child-lock adult-on)
CHILDLOCK_BYTES_LEN = 4
CHILDLOCK_STAGING_HI = CHILDLOCK_BYTES_ADDR + CHILDLOCK_BYTES_LEN

# "usb screen active" gate (written 1 by the stock "usb device" enter at
# 0x0200d4b0 and per-frame by the lock-screen path at 0x0200d880; cleared 0 by
# the stock exit at 0x0200d468).  While it is 1 the UI suppresses text/HUD
# rendering globally.  The replacement record skips the stock exit (which also
# powers the USB interface down), so the unlock must clear this flag itself or
# text stays dead until reboot.
USBSCREEN_FLAG_ADDR = 0x020CCD8C

# md5 of the documented original image (docs/UI_STATE_MACHINE.md); only used to
# annotate `stock-check` output - the stock-byte checks are the real gate.
FIRMWARE_MD5 = "7236c4c1bd3f12b1d85d33a9bebdf510"

# PERSISTENT (flash-image) variant of the two trampolines.  A flashed image has
# no RAM stubs, so the hijack words must point at code that is part of the
# image.  WARNING (2026-10-02 hardware night): 0x0207C640 (file 0x07EC40) is
# NOT a free zero pad - it is the 63x63 print_style_lut asset (764 nonzero
# entries in the live dump; img_stylize_gradient_lut reads it).  Any persistent
# recipe must move its stub host elsewhere.  Recipe only - nothing here builds
# or flashes an image.
PERSISTENT_GATE_STUB_ADDR = 0x0207C640   # file 0x07EC40
PERSISTENT_SEQ_STUB_ADDR = 0x0207C680    # file 0x07EC80
PERSISTENT_REGION_LO = 0x0207C640        # file 0x07EC40
PERSISTENT_REGION_HI = 0x0207DC68        # file 0x080268 (5672-byte zero pad)

# Hard RAM guard shared with kc02_usb.py (keep the two in sync).
RAM_POKE_LO = 0x020C0000
RAM_POKE_HI = 0x027FEC00

# Existing scratch window used by the shim tools; the unlock staging must not overlap.
SHIM_SCRATCH_LO = 0x020D8000
SHIM_SCRATCH_HI = 0x020D8300


def _in_ram(addr: int, length: int) -> bool:
    return RAM_POKE_LO <= addr and addr + length <= RAM_POKE_HI


def _in_staging(addr: int, length: int, lo: int, hi: int) -> bool:
    """True when [addr, addr+length) overlaps the half-open staging range [lo, hi)."""
    return addr + length > lo and addr < hi


# --------------------------------------------------------------------------
# The frame stub:  FUN_0204abc8(); 0x020130bc();  (protocol from the stock image)
# --------------------------------------------------------------------------
# Derived encoders, validated against the two known stock constants first.
_ENC_GEN_SW = lambda imm, src: struct.pack(
    "<I", 0xD7E00000 | (1 << 16) | (src << 11) | (imm & 0x7FF))
assert _ENC_GEN_SW(-4, 9) == ENC_SW_M4_R9, "sw encoder drift"
assert _ENC_GEN_SW(-8, 2) == ENC_SW_M8_R2, "sw encoder drift"
ENC_SW_M8_R3 = _ENC_GEN_SW(-8, 3)             # b.sw -0x8(r1),r3
ENC_LWZ_0_R3 = struct.pack("<I", 0x84610000)  # b.lwz r3,0x0(r1)


def _frame_stub_instructions(base: int = FRAME_STUB_ADDR):
    """(bytes, mnemonic, text) for the RAM frame stub at ``base``.

    The mode walker calls frame_fn(record->arg) with the arg in r3
    (FUN_02008188 @ 0x020082a8..0x020082bc).  bot_service_wrapper clobbers
    r3..r8, so the arg is saved/reloaded around the BOT call - without that
    reload the menu renderer received garbage as its state pointer and
    text/HUD rendering died globally while bare graphics kept painting.
    """
    return [
        (ENC_SW_M4_R9, "b.sw", "b.sw -0x4(r1),r9"),
        (ENC_SW_M8_R3, "b.sw", "b.sw -0x8(r1),r3"),
        (ENC_ADD_SP_N8, "b.addi", "b.addi r1,r1,-0x8"),
        (enc_jal(base + 0x0C, BOT_SERVICE_FN), "l.jal", "l.jal 0x0204d0a8"),
        (ENC_LWZ_0_R3, "b.lwz", "b.lwz r3,0x0(r1)"),
        (enc_jal(base + 0x14, MAINMENU_FRAME_FN), "l.jal", "l.jal 0x020130bc"),
        (ENC_ADD_SP_P8, "b.addi", "b.addi r1,r1,0x8"),
        (ENC_LWZ_M4_R9, "b.lwz", "b.lwz r9,-0x4(r1)"),
        (ENC_JR_R9, "l.jr", "l.jr r9"),
    ]


def build_frame_stub(base: int = FRAME_STUB_ADDR) -> bytes:
    if base % 4:
        raise ValueError("stub base must be word aligned")
    return b"".join(raw for raw, _m, _t in _frame_stub_instructions(base))


FRAME_STUB_BYTES = build_frame_stub(FRAME_STUB_ADDR)

if len(FRAME_STUB_BYTES) != FRAME_STUB_LEN:
    raise AssertionError(f"frame stub is {len(FRAME_STUB_BYTES)} B, expected {FRAME_STUB_LEN}")


def expected_frame_stub_disassembly(base: int = FRAME_STUB_ADDR):
    """(address, mnemonic, text, bytes) a correct disassembly must show."""
    out = []
    addr = base
    for raw, mnem, text in _frame_stub_instructions(base):
        out.append((addr, mnem, text, raw))
        addr += 4
    return out


# --------------------------------------------------------------------------
# The hoist stub:  service the BOT transport, then fall back into the hook
# --------------------------------------------------------------------------
def _hoist_stub_instructions(base: int = HOIST_STUB_ADDR):
    """(bytes, mnemonic, text) for the RAM hoist stub at ``base``."""
    return [
        (ENC_SW_M8_R2, "b.sw", "b.sw -0x8(r1),r2"),
        (ENC_SW_M4_R9, "b.sw", "b.sw -0x4(r1),r9"),
        (ENC_ADD_SP_N8, "b.addi", "b.addi r1,r1,-0x8"),
        (enc_jal(base + 0x0C, BOT_SERVICE_FN), "l.jal", "l.jal 0x0204d0a8"),
        (ENC_ADD_SP_P8, "b.addi", "b.addi r1,r1,0x8"),
        (ENC_LWZ_M8_R2, "b.lwz", "b.lwz r2,-0x8(r1)"),
        (ENC_LWZ_M4_R9, "b.lwz", "b.lwz r9,-0x4(r1)"),
        (ENC_SW_M4_R9, "b.sw", "b.sw -0x4(r1),r9"),
        (enc_lj(base + 0x20, PER_FRAME_HOOK_RESUME), "l.j", "l.j 0x02000408"),
    ]


def build_hoist_stub(base: int = HOIST_STUB_ADDR) -> bytes:
    if base % 4:
        raise ValueError("hoist stub base must be word aligned")
    return b"".join(raw for raw, _m, _t in _hoist_stub_instructions(base))


HOIST_STUB_BYTES = build_hoist_stub(HOIST_STUB_ADDR)

if len(HOIST_STUB_BYTES) != HOIST_STUB_LEN:
    raise AssertionError(
        f"hoist stub is {len(HOIST_STUB_BYTES)} B, expected {HOIST_STUB_LEN}"
    )


def build_code_patch() -> bytes:
    """The single 4-byte code write: `l.j <HOIST_STUB_ADDR>` at the hook entry."""
    return enc_lj(PER_FRAME_HOOK_ADDR, HOIST_STUB_ADDR)


CODE_PATCH_BYTES = build_code_patch()

if len(CODE_PATCH_BYTES) != CODE_PATCH_SITES[0][1]:
    raise AssertionError("code patch length does not match its allowlist entry")


def expected_hoist_disassembly():
    """(address, mnemonic, text, bytes) a correct disassembly must show."""
    out = []
    addr = HOIST_STUB_ADDR
    for raw, mnem, text in _hoist_stub_instructions(HOIST_STUB_ADDR):
        out.append((addr, mnem, text, raw))
        addr += 4
    out.append((PER_FRAME_HOOK_ADDR, "l.j", f"l.j 0x{HOIST_STUB_ADDR:08x}", CODE_PATCH_BYTES))
    return out


def hoist_plan() -> List[Tuple[str, int, bytes]]:
    """The complete hoist patch: list of (label, cpu_address, bytes).

    Order matters: the RAM stub is written first so the code write can never
    point at an uninitialised stub.
    """
    writes: List[Tuple[str, int, bytes]] = [
        ("hoist stub", HOIST_STUB_ADDR, HOIST_STUB_BYTES),
        ("code patch", PER_FRAME_HOOK_ADDR, CODE_PATCH_BYTES),
    ]
    for label, addr, blob in writes:
        if not blob:
            raise AssertionError(f"{label}: empty write")
        if addr % 4:
            raise AssertionError(f"{label}: address 0x{addr:08X} not word aligned")
        if label == "code patch":
            if not is_code_patch_site(addr, len(blob)):
                raise AssertionError(
                    f"{label}: 0x{addr:08X}+{len(blob)} is not an enumerated "
                    "code patch site"
                )
            continue
        if not _in_ram(addr, len(blob)):
            raise AssertionError(
                f"{label}: 0x{addr:08X}+{len(blob)} outside RAM "
                f"0x{RAM_POKE_LO:08X}..0x{RAM_POKE_HI:08X}"
            )
        if addr + len(blob) > SHIM_SCRATCH_LO and addr < SHIM_SCRATCH_HI:
            raise AssertionError(f"{label}: overlaps the shim scratch window")
        if addr + len(blob) > UNLOCK_BASE and addr < UNLOCK_STAGING_HI:
            raise AssertionError(f"{label}: overlaps the unlock-usb staging area")
    return writes


def hoist_plan_from_firmware_reader(reader) -> List[Tuple[str, int, bytes]]:
    """Validate the stock bytes at EVERY enumerated code site using ``reader``."""
    for site, length, stock, label in CODE_PATCH_SITES:
        got = reader(site, length)
        if got != stock:
            raise ValueError(
                f"code patch site 0x{site:08X} ({label}) holds {got.hex()}, "
                f"expected stock {stock.hex()} - wrong firmware or already patched"
            )
    return hoist_plan()


# --------------------------------------------------------------------------
# TASK B: child-friendly lock - stubs, plans and byte-level validation
# --------------------------------------------------------------------------
def _gate_stub_instructions(base: int = GATE_STUB_ADDR):
    """(bytes, mnemonic, text) for the adult-gate trampoline at ``base``.

    Entered by ``l.j`` from the hijacked key_mode3_power_request_menu entry
    (0x02009F08), so:

      * r9 still holds key_event_dispatch's return address,
      * r3/r4/r5 still hold (payload, flag, &ev) - the stub must not touch them,
      * only r6/r7 (scratch-safe across calls) are used.

    Layout (13 instructions, 52 B):

        0x00 movhi r6,0x020d      r6 = 0x020d0000
        0x04 ori   r6,r6,0x9480   r6 = &CHILDLOCK_SEQ (0x020D9480)
        0x08 lbz   r7,0x0(r6)     r7 = CHILDLOCK_SEQ
        0x0C sfnei r7,0x0         F  = (SEQ != 0)
        0x10 bnf   +0x1C          no arm -> check the adult flag
        0x14 sb    0x0(r6),r0     consume the arm (one shot)
        0x18 l.j   pass
        0x1C lbz   r7,0x1(r6)     r7 = CHILDLOCK_FLAG   <- check_flag
        0x20 sfnei r7,0x0         F  = (FLAG != 0)
        0x24 bnf   fail           not the adult -> ignore the event
        0x28 sw    -0x4(r1),r9    <- pass: re-execute the displaced stock instr
        0x2C l.j   0x02009F0C     continue the stock handler with r3/r4/r5 intact
        0x30 l.jr  r9             <- fail: clean ignore, no stack change
    """
    check_flag = base + 0x1C
    pass_ = base + 0x28
    fail = base + 0x30
    return [
        (shim.enc_movhi(6, 0x020D), "b.movhi", "b.movhi r6,0x20d"),
        (enc_ori(6, 6, 0x9480), "b.ori", "b.ori r6,r6,0x9480"),
        (shim.enc_lbz(7, 6, 0), "b.lbz", "b.lbz r7,0x0(r6)"),
        (shim.enc_sfxi(shim.COND_SFNEI, 7, 0), "b.sfnei", "b.sfnei r7,0x0"),
        (shim.enc_b_bnf(base + 0x10, check_flag), "b.bnf",
         f"b.bnf 0x{check_flag:08x}"),
        (shim.enc_sb(6, 0, 0), "b.sb", "b.sb 0x0(r6),r0"),
        (enc_lj(base + 0x18, pass_), "l.j", f"l.j 0x{pass_:08x}"),
        (shim.enc_lbz(7, 6, 1), "b.lbz", "b.lbz r7,0x1(r6)"),
        (shim.enc_sfxi(shim.COND_SFNEI, 7, 0), "b.sfnei", "b.sfnei r7,0x0"),
        (shim.enc_b_bnf(base + 0x24, fail), "b.bnf", f"b.bnf 0x{fail:08x}"),
        (MENU_GATE_HANDLER_STOCK, "b.sw", "b.sw -0x4(r1),r9"),
        (enc_lj(base + 0x2C, MENU_GATE_HANDLER_RESUME), "l.j",
         f"l.j 0x{MENU_GATE_HANDLER_RESUME:08x}"),
        (ENC_JR_R9, "l.jr", "l.jr r9"),
    ]


def build_gate_stub(base: int = GATE_STUB_ADDR) -> bytes:
    if base % 4:
        raise ValueError("gate stub base must be word aligned")
    return b"".join(raw for raw, _m, _t in _gate_stub_instructions(base))


GATE_STUB_BYTES = build_gate_stub(GATE_STUB_ADDR)

if len(GATE_STUB_BYTES) != GATE_STUB_LEN:
    raise AssertionError(
        f"gate stub is {len(GATE_STUB_BYTES)} B, expected {GATE_STUB_LEN}"
    )


def expected_gate_stub_disassembly(base: int = GATE_STUB_ADDR):
    """(address, mnemonic, text, bytes) a correct disassembly must show."""
    out = []
    addr = base
    for raw, mnem, text in _gate_stub_instructions(base):
        out.append((addr, mnem, text, raw))
        addr += 4
    return out


def _seq_stub_instructions(base: int = SEQ_STUB_ADDR):
    """(bytes, mnemonic, text) for the UP-tap arming trampoline at ``base``.

    Entered by ``l.j`` from the hijacked mode-3 UP handler entry 0x02009890,
    which is bound to event id 0x1b by the mode-3 hook list at 0x020C2B50.
    Only r6/r7 are used; r3/r4/r5/r9 must survive, r1 is never touched.

        0x00 movhi r6,0x020d
        0x04 ori   r6,r6,0x9480   r6 = &CHILDLOCK_SEQ
        0x08 addi  r7,r0,0x1
        0x0C sb    0x0(r6),r7     CHILDLOCK_SEQ = 1 (arm the sequence)
        0x10 sw    -0xc(r1),r18   <- re-execute the displaced stock instruction
        0x14 l.j   0x02009894     resume the stock UP handler
    """
    return [
        (shim.enc_movhi(6, 0x020D), "b.movhi", "b.movhi r6,0x20d"),
        (enc_ori(6, 6, 0x9480), "b.ori", "b.ori r6,r6,0x9480"),
        (shim.enc_addi(7, 0, 1), "b.addi", "b.addi r7,r0,0x1"),
        (shim.enc_sb(6, 7, 0), "b.sb", "b.sb 0x0(r6),r7"),
        (UP_HANDLER_STOCK, "b.sw", "b.sw -0xc(r1),r18"),
        (enc_lj(base + 0x14, UP_HANDLER_RESUME), "l.j",
         f"l.j 0x{UP_HANDLER_RESUME:08x}"),
    ]


def build_seq_stub(base: int = SEQ_STUB_ADDR) -> bytes:
    if base % 4:
        raise ValueError("seq stub base must be word aligned")
    return b"".join(raw for raw, _m, _t in _seq_stub_instructions(base))


SEQ_STUB_BYTES = build_seq_stub(SEQ_STUB_ADDR)

if len(SEQ_STUB_BYTES) != SEQ_STUB_LEN:
    raise AssertionError(
        f"seq stub is {len(SEQ_STUB_BYTES)} B, expected {SEQ_STUB_LEN}"
    )


def expected_seq_stub_disassembly(base: int = SEQ_STUB_ADDR):
    """(address, mnemonic, text, bytes) a correct disassembly must show."""
    out = []
    addr = base
    for raw, mnem, text in _seq_stub_instructions(base):
        out.append((addr, mnem, text, raw))
        addr += 4
    return out


# The three code words the child-lock patch writes (each is one instruction).
BOOT_MODE_CODE_BYTES = BOOT_MODE_WORD_PATCH
MENU_GATE_HIJACK_BYTES = enc_lj(MENU_GATE_HANDLER_ADDR, GATE_STUB_ADDR)
UP_HIJACK_BYTES = enc_lj(UP_HANDLER_ADDR, SEQ_STUB_ADDR)

# SEQ = 0, FLAG = 0 (adult override off), two spare zero bytes.
CHILDLOCK_BYTES_ZERO = bytes(CHILDLOCK_BYTES_LEN)
# The aligned word that turns the adult override ON (FLAG = 1, arm cleared).
CHILDLOCK_BYTES_ADULT = bytes([0, 1, 0, 0])


# The child-lock patch's own (addr, length, stock, patched, label) records; the
# first three are exactly the three new CODE_PATCH_SITES entries.
CHILDLOCK_WORD_SITES = (
    (BOOT_MODE_WORD_ADDR, 4, BOOT_MODE_WORD_STOCK, BOOT_MODE_CODE_BYTES,
     "boot initial mode 6 (Main menu) -> 3 (camera)"),
    (MENU_GATE_HANDLER_ADDR, 4, MENU_GATE_HANDLER_STOCK, MENU_GATE_HIJACK_BYTES,
     "key_mode3_power_request_menu entry -> l.j gate stub"),
    (UP_HANDLER_ADDR, 4, UP_HANDLER_STOCK, UP_HIJACK_BYTES,
     "mode-3 UP handler entry -> l.j seq stub"),
)


for _site, _len, _stock, _patched, _label in CHILDLOCK_WORD_SITES:
    if not is_code_patch_site(_site, _len):
        raise AssertionError(f"child-lock site 0x{_site:08X}+{_len} not enumerated")
    if _patched == _stock:
        raise AssertionError(f"child-lock site 0x{_site:08X}: patch equals stock")


# RAM staging invariants, checked at import (fail before anything reaches a device).
if GATE_STUB_ADDR % 4 or SEQ_STUB_ADDR % 4 or CHILDLOCK_BYTES_ADDR % 4:
    raise AssertionError("child-lock staging must be word aligned")
if GATE_STUB_ADDR + GATE_STUB_LEN > SEQ_STUB_ADDR:
    raise AssertionError("gate stub overlaps the seq stub")
if SEQ_STUB_ADDR + SEQ_STUB_LEN > CHILDLOCK_BYTES_ADDR:
    raise AssertionError("seq stub overlaps the childlock bytes")
if CHILDLOCK_BYTES_ADDR != CHILDLOCK_SEQ_ADDR or CHILDLOCK_FLAG_ADDR != CHILDLOCK_SEQ_ADDR + 1:
    raise AssertionError("childlock flag bytes moved")
if _in_staging(GATE_STUB_ADDR, GATE_STUB_LEN, UNLOCK_BASE, HOIST_STAGING_HI):
    raise AssertionError("child-lock staging overlaps the unlock/hoist staging")
if _in_staging(GATE_STUB_ADDR, CHILDLOCK_STAGING_HI - GATE_STUB_ADDR,
               SHIM_SCRATCH_LO, SHIM_SCRATCH_HI):
    raise AssertionError("child-lock staging overlaps the shim scratch")
if not _in_ram(GATE_STUB_ADDR, CHILDLOCK_STAGING_HI - GATE_STUB_ADDR):
    raise AssertionError("child-lock staging outside the RAM poke window")


def childlock_plan() -> List[Tuple[str, int, bytes]]:
    """The complete child-lock patch, in write order: (label, cpu address, bytes).

    RAM first (stubs + flag bytes), then the three code words, so a hijack word
    can never point at a stub that has not been written yet.
    """
    writes: List[Tuple[str, int, bytes]] = [
        ("gate stub", GATE_STUB_ADDR, GATE_STUB_BYTES),
        ("seq stub", SEQ_STUB_ADDR, SEQ_STUB_BYTES),
        ("childlock bytes", CHILDLOCK_BYTES_ADDR, CHILDLOCK_BYTES_ZERO),
        ("code: boot mode 6->3", BOOT_MODE_WORD_ADDR, BOOT_MODE_CODE_BYTES),
        ("code: menu gate hijack", MENU_GATE_HANDLER_ADDR, MENU_GATE_HIJACK_BYTES),
        ("code: seq-tap hijack", UP_HANDLER_ADDR, UP_HIJACK_BYTES),
    ]
    return _validate_childlock_writes(writes)


def childlock_live_plan() -> List[Tuple[str, int, bytes]]:
    """The TRANSIENT extra write: jump the running UI straight to the camera.

    ui_mode_pending = 3 (0x020CCD50) is what ui_mode_request() itself writes, so
    the mode walker leaves the current mode and enters mode 3 on the next frame.
    It is NOT persistent and is re-derived from the (patched) boot code on the
    next power-up; it also does nothing for a camera that is not running the UI
    task yet (usb_sense_state==2 boots into mode 10 - pair with unlock-usb).
    """
    return _validate_childlock_writes(
        [("pending mode -> camera (transient)", MODE_PENDING_ADDR,
          struct.pack("<I", 3))]
    )


def childlock_restore_plan() -> List[Tuple[str, int, bytes]]:
    """Undo the child-lock patch: the three STOCK words, then zeroed RAM."""
    writes: List[Tuple[str, int, bytes]] = [
        ("code: boot word restore", BOOT_MODE_WORD_ADDR, BOOT_MODE_WORD_STOCK),
        ("code: menu gate restore", MENU_GATE_HANDLER_ADDR, MENU_GATE_HANDLER_STOCK),
        ("code: seq-tap restore", UP_HANDLER_ADDR, UP_HANDLER_STOCK),
        ("gate stub zero", GATE_STUB_ADDR, bytes(GATE_STUB_LEN)),
        ("seq stub zero", SEQ_STUB_ADDR, bytes(SEQ_STUB_LEN)),
        ("childlock bytes zero", CHILDLOCK_BYTES_ADDR, CHILDLOCK_BYTES_ZERO),
    ]
    return _validate_childlock_writes(writes)


def childlock_adult_word(adult: bool) -> bytes:
    """The aligned word to write at 0x020D9480 for adult-on / adult-off.

    Bytes 0/2/3 are zeroed in both directions: turning the override on or off
    also drops a pending UP arm, so adult-off really does lock the menu again.
    """
    return CHILDLOCK_BYTES_ADULT if adult else CHILDLOCK_BYTES_ZERO


def childlock_adult_plan(adult: bool) -> List[Tuple[str, int, bytes]]:
    label = "adult override on" if adult else "adult override off"
    return _validate_childlock_writes(
        [(label, CHILDLOCK_BYTES_ADDR, childlock_adult_word(adult))]
    )


def _validate_childlock_writes(writes: Sequence[Tuple[str, int, bytes]]):
    """Hard invariants for every child-lock write (called before any device I/O)."""
    for label, addr, blob in writes:
        if not blob:
            raise AssertionError(f"{label}: empty write")
        if addr % 4 or len(blob) % 4:
            raise AssertionError(
                f"{label}: 0x{addr:08X}+{len(blob)} is not 32-bit aligned"
            )
        if is_code_patch_site(addr, len(blob)):
            continue
        if not _in_ram(addr, len(blob)):
            raise AssertionError(
                f"{label}: 0x{addr:08X}+{len(blob)} outside RAM "
                f"0x{RAM_POKE_LO:08X}..0x{RAM_POKE_HI:08X} and not an enumerated "
                "code patch site"
            )
        if _in_staging(addr, len(blob), SHIM_SCRATCH_LO, SHIM_SCRATCH_HI):
            raise AssertionError(f"{label}: overlaps the shim scratch window")
        if _in_staging(addr, len(blob), UNLOCK_BASE, HOIST_STAGING_HI):
            raise AssertionError(f"{label}: overlaps the unlock/hoist staging")
    return list(writes)


def verify_childlock_stock(reader) -> None:
    """Abort unless ``reader`` (addr, len) -> bytes shows the STOCK words."""
    for site, length, stock, _patched, label in CHILDLOCK_WORD_SITES:
        got = reader(site, length)
        if got != stock:
            raise ValueError(
                f"child-lock site 0x{site:08X} ({label}) holds {got.hex()}, "
                f"expected stock {stock.hex()} - wrong firmware, or already "
                "patched (use `child-lock --restore` first)"
            )


def childlock_plan_from_firmware_reader(reader) -> List[Tuple[str, int, bytes]]:
    """Validate the three stock words through ``reader``, then return the plan."""
    verify_childlock_stock(reader)
    return childlock_plan()


def childlock_site_state(words: Sequence[bytes]) -> str:
    """Classify the three code sites from their current 3 x 4 bytes.

    Returns "stock", "patched" or "unknown" - used by ``child-lock`` to refuse
    to write over a firmware state it does not recognise.
    """
    if len(words) != len(CHILDLOCK_WORD_SITES):
        raise ValueError("expected one word per child-lock site")
    if all(got == site[2] for got, site in zip(words, CHILDLOCK_WORD_SITES)):
        return "stock"
    if all(got == site[3] for got, site in zip(words, CHILDLOCK_WORD_SITES)):
        return "patched"
    return "unknown"


def build_mode_record(stock_rec: bytes, frame: int = FRAME_STUB_ADDR) -> bytes:
    """Patch a stock Main-menu record into the "usb device" record.

    ``stock_rec`` is the 0x100 bytes read from MAINMENU_REC_ADDR at runtime, so
    the patch does not hard-code any firmware-version-specific field.  Only the
    exit slot (-> 0, so leaving the mode never disables the UI/USB) and the
    frame slot (-> our RAM stub) are changed.
    """
    if len(stock_rec) != MAINMENU_REC_LEN:
        raise ValueError(
            f"stock record must be {MAINMENU_REC_LEN} bytes, got {len(stock_rec)}"
        )
    rec = bytearray(stock_rec)
    enter = struct.unpack_from("<I", rec, 0x08)[0]
    if enter == 0:
        raise ValueError(
            "stock Main-menu record has a NULL enter function - refusing to patch"
        )
    if struct.unpack_from("<I", rec, REC_OFF_FRAME)[0] == frame:
        raise ValueError("stock record already points at the stub - wrong address?")
    struct.pack_into("<I", rec, REC_OFF_EXIT, 0)
    struct.pack_into("<I", rec, REC_OFF_FRAME, frame)
    return bytes(rec)


def plan(stock_rec: bytes) -> List[Tuple[str, int, bytes]]:
    """The complete, ordered patch: list of (label, cpu_address, bytes)."""
    rec = build_mode_record(stock_rec)
    writes: List[Tuple[str, int, bytes]] = [
        ("frame stub", FRAME_STUB_ADDR, FRAME_STUB_BYTES),
        ("mode-10 record", REC_ADDR, rec[:0x18]),
        ("mode table[10]", MODE_TABLE_SLOT, struct.pack("<I", REC_ADDR)),
        # Point the *active* record at ours before switching modes, so the mode
        # machine's "exit current record" step runs the patch's exit (0) and
        # never calls 0x0200d450 (which would power the USB interface down).
        ("active mode record", MODE_ACTIVE_REC_ADDR, struct.pack("<I", REC_ADDR)),
        # Re-enter mode 10 so the patched record is used from the next frame on.
        ("pending mode", MODE_PENDING_ADDR, struct.pack("<I", USBMODE_INDEX)),
        # Clear the usb-screen gate so text/HUD rendering comes back (the stock
        # exit would do this, but it also powers the USB interface down).
        ("usb-screen flag", USBSCREEN_FLAG_ADDR, struct.pack("<I", 0)),
    ]

    # --- hard invariant checks (fail before anything is sent to a device) ----
    for label, addr, blob in writes:
        if not blob:
            raise AssertionError(f"{label}: empty write")
        if addr % 4:
            raise AssertionError(f"{label}: address 0x{addr:08X} not word aligned")
        if not _in_ram(addr, len(blob)):
            raise AssertionError(
                f"{label}: 0x{addr:08X}+{len(blob)} outside RAM "
                f"0x{RAM_POKE_LO:08X}..0x{RAM_POKE_HI:08X}"
            )
        if addr + len(blob) > SHIM_SCRATCH_LO and addr < SHIM_SCRATCH_HI:
            raise AssertionError(f"{label}: overlaps the shim scratch window")
    if MODE_TABLE_SLOT != 0x020CCD84:
        raise AssertionError("mode-table slot arithmetic changed")
    return writes


def plan_from_firmware_reader(reader) -> List[Tuple[str, int, bytes]]:
    """Build the plan using ``reader`` (callable addr,length -> bytes)."""
    return plan(reader(MAINMENU_REC_ADDR, MAINMENU_REC_LEN))


# --------------------------------------------------------------------------
# Offline helpers / self-test
# --------------------------------------------------------------------------
def hexdump(data: bytes, base: int) -> str:
    lines = []
    for i in range(0, len(data), 16):
        row = data[i:i + 16]
        lines.append(
            f"  0x{base + i:08X}  " + " ".join(f"{b:02x}" for b in row)
        )
    return "\n".join(lines)


def _decode_jal(inst_addr: int, raw: bytes) -> int:
    word = struct.unpack("<I", raw)[0]
    if (word >> 26) != 0x01:
        raise ValueError("not an l.jal")
    imm = word & 0x03FFFFFF
    if imm & 0x02000000:
        imm -= 0x04000000
    return (inst_addr + (imm << 2)) & 0xFFFFFFFF


def _decode_lj(inst_addr: int, raw: bytes) -> int:
    word = struct.unpack("<I", raw)[0]
    if (word >> 26) != 0x00:
        raise ValueError("not an l.j")
    imm = word & 0x03FFFFFF
    if imm & 0x02000000:
        imm -= 0x04000000
    return (inst_addr + (imm << 2)) & 0xFFFFFFFF


def _decode_branch(inst_addr: int, raw: bytes) -> int:
    """Decode the target of a ``b.bf`` (0x04) / ``b.bnf`` (0x03) instruction."""
    word = struct.unpack("<I", raw)[0]
    if (word >> 26) not in (0x03, 0x04):
        raise ValueError("not a b.bf/b.bnf")
    imm = word & 0x03FFFFFF
    if imm & 0x02000000:
        imm -= 0x04000000
    return (inst_addr + (imm << 2)) & 0xFFFFFFFF


def selftest() -> int:
    fails = 0

    def check(cond: bool, msg: str) -> None:
        nonlocal fails
        if cond:
            print(f"ok    {msg}")
        else:
            print(f"FAIL  {msg}")
            fails += 1

    def raises(fn, *a, **kw) -> bool:
        try:
            fn(*a, **kw)
        except Exception:
            return True
        return False

    # --- stub shape ------------------------------------------------------
    stub = build_frame_stub(FRAME_STUB_ADDR)
    check(len(stub) == FRAME_STUB_LEN, "frame stub length")
    check(stub[:4] == ENC_SW_M4_R9, "stub saves LR with the stock idiom")
    check(stub[4:8] == ENC_SW_M8_R3, "stub saves the frame arg before the BOT call")
    check(stub[16:20] == ENC_LWZ_0_R3, "stub reloads the frame arg before the menu call")
    check(stub[-8:-4] == ENC_LWZ_M4_R9 and stub[-4:] == ENC_JR_R9, "stub epilogue")
    check(_decode_jal(FRAME_STUB_ADDR + 0x0C, stub[12:16]) == BOT_POLL_FN,
          "stub l.jal -> BOT poll 0x0204abc8")
    check(_decode_jal(FRAME_STUB_ADDR + 0x14, stub[20:24]) == MAINMENU_FRAME_FN,
          "stub l.jal -> stock Main-menu frame 0x020130bc")
    # opcode sanity: only sw (0x35) / addi (0x27) / lwz (0x21) / jal (0x01)
    # / jr (0x11) appear in the stub
    ops = {struct.unpack_from("<I", stub, i)[0] >> 26 for i in range(0, 36, 4)}
    check(ops <= {0x35, 0x27, 0x21, 0x01, 0x11},
          f"stub opcode set {sorted(hex(o) for o in ops)}")

    # --- record patch ----------------------------------------------------
    fake = bytes(MAINMENU_REC_LEN)  # all zero
    fake = bytearray(fake)
    struct.pack_into("<I", fake, 0x08, MAINMENU_ENTER_FN)
    struct.pack_into("<I", fake, 0x10, MAINMENU_FRAME_FN)
    fake = bytes(fake)
    rec = build_mode_record(fake)
    check(struct.unpack_from("<I", rec, REC_OFF_EXIT)[0] == 0, "record exit slot zeroed")
    check(struct.unpack_from("<I", rec, REC_OFF_FRAME)[0] == FRAME_STUB_ADDR,
          "record frame slot -> stub")
    check(struct.unpack_from("<I", rec, 0x08)[0] == MAINMENU_ENTER_FN,
          "record enter slot untouched")
    check(rec[:0x0C] == fake[:0x0C] and rec[0x14:] == fake[0x14:],
          "only the two patched slots changed")
    check(raises(build_mode_record, fake[:0x40]), "short stock record rejected")
    check(raises(build_mode_record, bytes(MAINMENU_REC_LEN)),
          "NULL enter function rejected")

    # --- whole-plan invariants -------------------------------------------
    writes = plan(fake)
    check(len(writes) == 5, "plan has five writes")
    check(all(_in_ram(a, len(b)) for _l, a, b in writes), "every write inside RAM guard")
    check(all(not (a + len(b) > SHIM_SCRATCH_LO and a < SHIM_SCRATCH_HI)
              for _l, a, b in writes), "no write overlaps the shim scratch window")
    check(MODE_TABLE_SLOT == 0x020CCD84, "mode-table[10] slot address")
    check(RAM_POKE_LO <= UNLOCK_BASE and REC_ADDR + MAINMENU_REC_LEN <= RAM_POKE_HI,
          "staging area inside the RAM guard")
    check(not (UNLOCK_BASE + 0x200 > SHIM_SCRATCH_LO and UNLOCK_BASE < SHIM_SCRATCH_HI),
          "staging area clear of the shim scratch")
    # the three bookkeeping writes are u32 mode/record pointers
    labels = [l for l, _a, _b in writes]
    check(labels == ["frame stub", "mode-10 record", "mode table[10]",
                     "active mode record", "pending mode"], "plan order")

    # --- no patch byte may embed a flash-modifying routine address ---------
    forbidden_words = {struct.pack("<I", f) for f in FORBIDDEN_FUNCTIONS}
    for label, _addr, blob in writes:
        embedded = [off for off in range(0, max(0, len(blob) - 3))
                    if blob[off:off + 4] in forbidden_words]
        check(not embedded, f"'{label}' embeds no forbidden routine address")

    # --- HOIST (TASK A): stub shape -------------------------------------
    hstub = build_hoist_stub(HOIST_STUB_ADDR)
    check(len(hstub) == HOIST_STUB_LEN, f"hoist stub length {len(hstub)}")
    check(hstub[0:4] == ENC_SW_M8_R2 and hstub[4:8] == ENC_SW_M4_R9
          and hstub[8:12] == ENC_ADD_SP_N8,
          "hoist stub uses the save-first prologue idiom (r2@-8, r9@-4, then alloc)")
    check(hstub[12:16] == enc_jal(HOIST_STUB_ADDR + 0x0C, BOT_SERVICE_FN),
          "hoist stub l.jal -> pure BOT service 0x0204d0a8")
    check(_decode_jal(HOIST_STUB_ADDR + 0x0C, hstub[12:16]) == BOT_SERVICE_FN,
          "hoist stub l.jal target decodes to 0x0204d0a8")
    check(hstub[16:20] == ENC_ADD_SP_P8, "hoist stub deallocates before reloading")
    check(hstub[20:24] == ENC_LWZ_M8_R2 and hstub[24:28] == ENC_LWZ_M4_R9,
          "hoist stub reloads r2/r9 from the same offsets it spilled them")
    check(hstub[28:32] == PER_FRAME_HOOK_STOCK,
          "hoist stub re-executes the instruction it displaced")
    check(_decode_lj(HOIST_STUB_ADDR + 0x20, hstub[32:36]) == PER_FRAME_HOOK_RESUME,
          "hoist stub l.j returns into the stock hook body at 0x02000408")
    hops = {struct.unpack_from("<I", hstub, i)[0] >> 26 for i in range(0, HOIST_STUB_LEN, 4)}
    check(hops <= {0x35, 0x27, 0x21, 0x01, 0x00},
          f"hoist stub opcode set {sorted(hex(o) for o in hops)}")
    # it must not call the panel/JPEG wrapper (side effect stays where stock had it)
    check(_decode_jal(HOIST_STUB_ADDR + 0x0C, hstub[12:16]) != BOT_WRAPPER_FN,
          "hoist stub calls the pure BOT service, not the panel-pumping wrapper")

    # --- HOIST: the one code write ----------------------------------------
    check(CODE_PATCH_BYTES == enc_lj(PER_FRAME_HOOK_ADDR, HOIST_STUB_ADDR),
          "code patch is l.j <hoist stub> from the hook entry")
    check(_decode_lj(PER_FRAME_HOOK_ADDR, CODE_PATCH_BYTES) == HOIST_STUB_ADDR,
          "code patch target decodes back to the hoist stub")
    check(len(CODE_PATCH_BYTES) == 4, "the code patch is exactly one instruction")

    # --- HOIST: the code-site allowlist is EXACT ---------------------------
    check(len(CODE_PATCH_SITES) == 4,
          "four code patch sites are enumerated (hoist + three child-lock)")
    check(is_code_patch_site(PER_FRAME_HOOK_ADDR, 4),
          "the enumerated site is admitted")
    for bad_addr, bad_len in (
        (PER_FRAME_HOOK_ADDR, 8),      # a wider write at the same address
        (PER_FRAME_HOOK_ADDR, 1),      # narrower
        (PER_FRAME_HOOK_ADDR, 3),
        (PER_FRAME_HOOK_ADDR, 5),
        (PER_FRAME_HOOK_RESUME, 4),    # the next instruction
        (PER_FRAME_HOOK_ADDR - 4, 4),  # the instruction before it
        (PER_FRAME_HOOK_ADDR + 0x30, 4),  # the hook's epilogue
        (0x02000000, 4), (0x020082A4, 4), (0x020082BC, 8),
        (0x02008188, 4), (0x0200046C, 4), (0x020130BC, 4), (0x020130E4, 4),
        (0x0200D588, 4), (0x0200D450, 4), (0x0200D498, 4), (0x02006C74, 4),
        (0x02004944, 4), (0x0203D38C, 4), (0x0207B7FC, 4),
    ):
        check(not is_code_patch_site(bad_addr, bad_len),
              f"code site allowlist must reject 0x{bad_addr:08X}+{bad_len}")
    check(not is_code_patch_site(HOIST_STUB_ADDR, 4),
          "the RAM stub address is not a code site (it goes through the RAM guard)")
    check(PER_FRAME_HOOK_ADDR + 4 <= 0x0207B800,
          "the code patch site is inside the app code region")

    # --- HOIST: plan invariants -------------------------------------------
    hplan = hoist_plan()
    check([l for l, _a, _b in hplan] == ["hoist stub", "code patch"],
          "hoist plan writes the RAM stub before the code patch")
    check(hplan[0][1] == HOIST_STUB_ADDR and hplan[1][1] == PER_FRAME_HOOK_ADDR,
          "hoist plan addresses")
    check(_in_ram(HOIST_STUB_ADDR, HOIST_STUB_LEN), "hoist stub inside the RAM guard")
    check(not (HOIST_STUB_ADDR + HOIST_STUB_LEN > SHIM_SCRATCH_LO
               and HOIST_STUB_ADDR < SHIM_SCRATCH_HI),
          "hoist stub clear of the shim scratch")
    check(not (HOIST_STUB_ADDR + HOIST_STUB_LEN > UNLOCK_BASE
               and HOIST_STUB_ADDR < UNLOCK_STAGING_HI),
          "hoist stub clear of the unlock-usb staging area")

    # --- HOIST: no patch byte may embed a flash-modifying routine address ---
    forbidden_words_h = {struct.pack("<I", f) for f in FORBIDDEN_FUNCTIONS}
    for label, _addr, blob in hplan:
        embedded = [off for off in range(0, max(0, len(blob) - 3))
                    if blob[off:off + 4] in forbidden_words_h]
        check(not embedded, f"'{label}' embeds no forbidden routine address")

    # --- HOIST: stock-byte validation of the code site ---------------------
    stock_reader = lambda a, n: expected_code_site_bytes().get(
        a, PER_FRAME_HOOK_STOCK)
    check(len(hoist_plan_from_firmware_reader(stock_reader)) == 2,
          "hoist plan accepts the stock code-site bytes")
    check(raises(hoist_plan_from_firmware_reader,
                 lambda a, n: b"\x00\x00\x00\x00"),
          "hoist plan rejects unexpected bytes at the code site")
    check(expected_code_site_bytes()[PER_FRAME_HOOK_ADDR] == PER_FRAME_HOOK_STOCK,
          "the code site's expected stock bytes are the stock prologue store")

    # --- TASK B: child-lock stub shape -------------------------------------
    gate = build_gate_stub(GATE_STUB_ADDR)
    check(len(gate) == GATE_STUB_LEN, f"gate stub length {len(gate)}")
    check(gate[0:4] == shim.enc_movhi(6, 0x020D), "gate stub builds 0x020dxxxx")
    check(gate[4:8] == enc_ori(6, 6, 0x9480), "gate stub r6 = 0x020d9480")
    check(gate[8:12] == shim.enc_lbz(7, 6, 0), "gate stub reads CHILDLOCK_SEQ")
    check(gate[0x1C:0x20] == shim.enc_lbz(7, 6, 1), "gate stub reads CHILDLOCK_FLAG")
    check(gate[0x28:0x2C] == MENU_GATE_HANDLER_STOCK,
          "gate stub re-executes the instruction it displaced at 0x02009f08")
    check(gate[0x2C:0x30] == enc_lj(GATE_STUB_ADDR + 0x2C, MENU_GATE_HANDLER_RESUME),
          "gate stub resumes the stock handler at 0x02009f0c")
    check(gate[-4:] == ENC_JR_R9, "gate stub fail path is a bare l.jr r9")
    check(_decode_lj(GATE_STUB_ADDR + 0x2C, gate[0x2C:0x30]) == MENU_GATE_HANDLER_RESUME,
          "gate stub return l.j decodes back to 0x02009f0c")
    _gops = {struct.unpack_from("<I", gate, i)[0] >> 26 for i in range(0, GATE_STUB_LEN, 4)}
    check(_gops <= {0x06, 0x2A, 0x23, 0x2F, 0x03, 0x36, 0x00, 0x35, 0x11},
          f"gate stub opcode set {sorted(hex(o) for o in _gops)}")
    check(0x01 not in _gops, "gate stub has no l.jal (r9 must survive every path)")
    check(0x27 not in _gops, "gate stub never adjusts r1 (no stack traffic)")
    check(gate[0x14:0x18] == shim.enc_sb(6, 0, 0),
          "gate pass path clears CHILDLOCK_SEQ (one-shot arm)")
    _bd = lambda off: _decode_branch(GATE_STUB_ADDR + off, gate[off:off + 4])
    check(_bd(0x10) == GATE_STUB_ADDR + 0x1C, "SEQ-clear branch goes to the FLAG test")
    check(_bd(0x24) == GATE_STUB_ADDR + 0x30, "FLAG-clear branch goes to the fail path")
    check(struct.unpack_from("<I", gate, 0x18)[0] >> 26 == 0x00
          and _decode_lj(GATE_STUB_ADDR + 0x18, gate[0x18:0x1C]) == GATE_STUB_ADDR + 0x28,
          "the armed path joins the pass path")

    sgate = build_seq_stub(SEQ_STUB_ADDR)
    check(len(sgate) == SEQ_STUB_LEN, f"seq stub length {len(sgate)}")
    check(sgate[0:4] == shim.enc_movhi(6, 0x020D) and sgate[4:8] == enc_ori(6, 6, 0x9480),
          "seq stub builds the same 0x020d9480 pointer")
    check(sgate[8:12] == shim.enc_addi(7, 0, 1), "seq stub loads the value 1")
    check(sgate[12:16] == shim.enc_sb(6, 7, 0), "seq stub arms CHILDLOCK_SEQ")
    check(sgate[16:20] == UP_HANDLER_STOCK,
          "seq stub re-executes the instruction it displaced at 0x02009890")
    check(_decode_lj(SEQ_STUB_ADDR + 0x14, sgate[20:24]) == UP_HANDLER_RESUME,
          "seq stub resumes the stock UP handler at 0x02009894")
    _sops = {struct.unpack_from("<I", sgate, i)[0] >> 26 for i in range(0, SEQ_STUB_LEN, 4)}
    check(_sops <= {0x06, 0x2A, 0x27, 0x36, 0x35, 0x00},
          f"seq stub opcode set {sorted(hex(o) for o in _sops)}")
    check(0x01 not in _sops, "seq stub has no l.jal")
    check(SEQ_STUB_ADDR + SEQ_STUB_LEN <= CHILDLOCK_BYTES_ADDR,
          "seq stub ends before the childlock flag bytes")
    check(GATE_STUB_ADDR + GATE_STUB_LEN <= SEQ_STUB_ADDR,
          "gate stub ends before the seq stub")

    # --- TASK B: the three code words --------------------------------------
    check(BOOT_MODE_WORD_STOCK == bytes.fromhex("0600609c"),
          "stock boot-mode word is b.addi r3,r0,0x6")
    check(BOOT_MODE_WORD_PATCH == bytes.fromhex("0300609c"),
          "patched boot-mode word is b.addi r3,r0,0x3")
    check(BOOT_MODE_WORD_PATCH[:1] == bytes([3]) and BOOT_MODE_WORD_PATCH[2:] == bytes([0x60, 0x9C]),
          "only the immediate changed in the boot word")
    check(_decode_lj(MENU_GATE_HANDLER_ADDR, MENU_GATE_HIJACK_BYTES) == GATE_STUB_ADDR,
          "menu-gate hijack word is l.j <gate stub>")
    check(_decode_lj(UP_HANDLER_ADDR, UP_HIJACK_BYTES) == SEQ_STUB_ADDR,
          "seq-tap hijack word is l.j <seq stub>")
    check(RAM_POKE_LO <= GATE_STUB_ADDR < RAM_POKE_HI
          and RAM_POKE_LO <= SEQ_STUB_ADDR < RAM_POKE_HI,
          "both hijack targets are inside the RAM poke window")
    for site, _l, stock, patched, label in CHILDLOCK_WORD_SITES:
        check(len(stock) == 4 and len(patched) == 4, f"{label}: one word each")
        check(struct.unpack("<I", patched)[0] >> 26 in (0x00, 0x27),
              f"{label}: patched word is a plain l.j / b.addi")

    # --- TASK B: the allowlist admits exactly the four sites ---------------
    _cl_sites = [s for s, _l, _b, _lb in CHILDLOCK_CODE_SITES]
    check(_cl_sites == [BOOT_MODE_WORD_ADDR, MENU_GATE_HANDLER_ADDR, UP_HANDLER_ADDR],
          "the three child-lock sites are the code sites in write order")
    check(all(is_code_patch_site(s, 4) for s in _cl_sites),
          "every child-lock site is admitted")
    for s in _cl_sites:
        for a, ln in ((s, 1), (s, 3), (s, 5), (s, 8), (s - 4, 4), (s + 4, 4),
                      (s - 4, 4), (s + 0x28, 4), (0x02000494, 4), (0x020004B0, 4),
                      (0x02009F0C, 4), (0x02009894, 4), (0x020098A8, 4),
                      (0x02009F04, 4), (0x02009F6C, 4), (0x0200A4BC, 4)):
            check(not is_code_patch_site(a, ln),
                  f"child-lock allowlist must reject 0x{a:08X}+{ln}")
    check(not is_code_patch_site(GATE_STUB_ADDR, 4),
          "the gate stub address is RAM, not a code site")
    check(all(s < 0x0207B800 for s in _cl_sites),
          "every child-lock code site lies in the app code region")
    check(all(s not in FORBIDDEN_FUNCTIONS for s in _cl_sites),
          "no child-lock site is a flash routine")

    # --- TASK B: plan invariants ------------------------------------------
    cplan = childlock_plan()
    check([l for l, _a, _b in cplan] == [
        "gate stub", "seq stub", "childlock bytes",
        "code: boot mode 6->3", "code: menu gate hijack", "code: seq-tap hijack"],
        "child-lock plan order (RAM stubs before the code words)")
    check([a for _l, a, _b in cplan] == [
        GATE_STUB_ADDR, SEQ_STUB_ADDR, CHILDLOCK_BYTES_ADDR,
        BOOT_MODE_WORD_ADDR, MENU_GATE_HANDLER_ADDR, UP_HANDLER_ADDR],
        "child-lock plan addresses")
    for label, addr, blob in cplan:
        if is_code_patch_site(addr, len(blob)):
            continue
        check(_in_ram(addr, len(blob)), f"'{label}' inside the RAM guard")
        check(not _in_staging(addr, len(blob), SHIM_SCRATCH_LO, SHIM_SCRATCH_HI),
              f"'{label}' clear of the shim scratch")
        check(not _in_staging(addr, len(blob), UNLOCK_BASE, HOIST_STAGING_HI),
              f"'{label}' clear of the unlock/hoist staging")
    check(_in_staging(CHILDLOCK_BYTES_ADDR, 4, UNLOCK_BASE, HOIST_STAGING_HI) is False,
          "the flag bytes are clear of UNLOCK_BASE..0x020D9223")
    check(GATE_STUB_ADDR >= 0x020D9400 and CHILDLOCK_STAGING_HI == 0x020D9484,
          "child-lock staging extent")

    # --- TASK B: no byte of the patch may embed a forbidden routine --------
    forbidden_words_cl = {struct.pack("<I", f) for f in FORBIDDEN_FUNCTIONS}
    for label, _addr, blob in cplan:
        embedded = [off for off in range(0, max(0, len(blob) - 3))
                    if blob[off:off + 4] in forbidden_words_cl]
        check(not embedded, f"'{label}' embeds no forbidden routine address")
    for label, _addr, blob in childlock_restore_plan():
        embedded = [off for off in range(0, max(0, len(blob) - 3))
                    if blob[off:off + 4] in forbidden_words_cl]
        check(not embedded, f"restore '{label}' embeds no forbidden routine")

    # --- TASK B: transient live jump --------------------------------------
    live = childlock_live_plan()
    check(len(live) == 1 and live[0][1] == MODE_PENDING_ADDR
          and live[0][2] == struct.pack("<I", 3),
          "live jump writes ui_mode_pending = 3 (camera) at 0x020ccd50")
    check(MODE_PENDING_ADDR == 0x020CCD50, "ui_mode_pending address")

    # --- TASK B: adult flag word ------------------------------------------
    check(childlock_adult_word(True) == bytes([0, 1, 0, 0]), "adult-on word")
    check(childlock_adult_word(False) == bytes(4), "adult-off word")
    check(CHILDLOCK_FLAG_ADDR == CHILDLOCK_SEQ_ADDR + 1,
          "the two control bytes are adjacent (seq at +0, flag at +1)")
    for _adult in (True, False):
        _w = childlock_adult_plan(_adult)
        check(len(_w) == 1 and _w[0][1] == CHILDLOCK_BYTES_ADDR and len(_w[0][2]) == 4,
              f"adult {'on' if _adult else 'off'} writes one aligned word")

    # --- TASK B: stock-byte validation + mismatch aborts -------------------
    stock_map = {s: st for s, _l, st, _p, _lb in CHILDLOCK_WORD_SITES}
    good_reader = lambda a, n: stock_map.get(a, b"\xff" * n)
    check(len(childlock_plan_from_firmware_reader(good_reader)) == 6,
          "child-lock plan accepted with the stock words")
    check(raises(childlock_plan_from_firmware_reader,
                 lambda a, n: bytes(n)),
          "child-lock plan aborts when the stock bytes do not match")
    check(raises(childlock_plan_from_firmware_reader,
                 lambda a, n: BOOT_MODE_WORD_PATCH if a == BOOT_MODE_WORD_ADDR
                 else stock_map.get(a, b"\xff" * n)),
          "child-lock plan aborts on an already-patched boot word")
    check(raises(verify_childlock_stock, lambda a, n: bytes(n)),
          "verify_childlock_stock aborts on a mismatch")
    try:
        verify_childlock_stock(good_reader)
        check(True, "verify_childlock_stock accepts the stock words")
    except Exception as exc:  # pragma: no cover - only on a real failure
        check(False, f"verify_childlock_stock rejected the stock words: {exc}")

    # --- TASK B: restore returns exactly the original words ----------------
    rplan = childlock_restore_plan()
    check([l for l, _a, _b in rplan] == [
        "code: boot word restore", "code: menu gate restore", "code: seq-tap restore",
        "gate stub zero", "seq stub zero", "childlock bytes zero"],
        "restore plan order (code first, then zeroed RAM)")
    for (site, _l, stock, _p, label), (_l2, addr, blob) in zip(CHILDLOCK_WORD_SITES, rplan[:3]):
        check(addr == site and blob == stock, f"restore of {label} writes the stock word")
    for _l2, _addr, blob in rplan[3:]:
        check(blob == bytes(len(blob)), f"restore '{_l2}' zeroes RAM")

    # --- TASK B: end-to-end simulation (apply, then restore) ---------------
    mem = {site: stock for site, _l, stock, _p, _lb in CHILDLOCK_WORD_SITES}
    mem.update({GATE_STUB_ADDR: bytes(GATE_STUB_LEN), SEQ_STUB_ADDR: bytes(SEQ_STUB_LEN),
                CHILDLOCK_BYTES_ADDR: bytes(4)})
    original = dict(mem)
    for _l, addr, blob in childlock_plan():
        mem[addr] = blob
    check(childlock_site_state([mem[s] for s, _l, _s, _p, _lb in CHILDLOCK_WORD_SITES]) == "patched",
          "simulated apply leaves the three sites in the patched state")
    check(mem[CHILDLOCK_BYTES_ADDR] == bytes(4), "simulated apply clears both control bytes")
    for _l, addr, blob in childlock_restore_plan():
        mem[addr] = blob
    check(mem == original, "restore returns every touched address to its original bytes")
    check(childlock_site_state([mem[s] for s, _l, _s, _p, _lb in CHILDLOCK_WORD_SITES]) == "stock",
          "the sites classify as stock after a restore")
    check(childlock_site_state([bytes(4), bytes(4), bytes(4)]) == "unknown",
          "a foreign site state classifies as unknown (the CLI refuses it)")

    # --- TASK B: stub disassembly expectations + shared shim encoders ------
    check(len(expected_gate_stub_disassembly()) == 13, "gate stub = 13 instructions")
    check(len(expected_seq_stub_disassembly()) == 6, "seq stub = 6 instructions")
    check(all(len(e[3]) == 4 for e in expected_gate_stub_disassembly()),
          "every gate stub instruction is one word")
    _shim_rc = None
    import io as _io2
    _buf2 = _io2.StringIO()
    _old = sys.stdout
    sys.stdout = _buf2
    try:
        _shim_rc = shim.selftest()
    finally:
        sys.stdout = _old
    check(_shim_rc == 0, "kc02_shim selftest (encoders vs harvested image bytes)")

    print(f"\n{len(writes)} writes, "
          f"{sum(len(b) for _l, _a, b in writes)} bytes total")
    return 1 if fails else 0


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def expected_disassembly():
    return expected_frame_stub_disassembly(FRAME_STUB_ADDR)


def expected_childlock_disassembly():
    """(address, mnemonic, text, bytes) a correct disassembly of the whole
    child-lock patch must show: gate stub, seq stub, then the three code words."""
    out = list(expected_gate_stub_disassembly())
    out += list(expected_seq_stub_disassembly())
    out.append((BOOT_MODE_WORD_ADDR, "b.addi", "b.addi r3,r0,0x3",
                BOOT_MODE_CODE_BYTES))
    out.append((MENU_GATE_HANDLER_ADDR, "l.j",
                f"l.j 0x{GATE_STUB_ADDR:08x}", MENU_GATE_HIJACK_BYTES))
    out.append((UP_HANDLER_ADDR, "l.j",
                f"l.j 0x{SEQ_STUB_ADDR:08x}", UP_HIJACK_BYTES))
    return out


def verify_childlock_roundtrip(path: str) -> int:
    """Check a Ghidra dump of both child-lock stubs + the three code words.

    ``path`` holds TSV lines ``0xADDR<TAB>mnemonic<TAB>text<TAB>hexbytes`` for
    the 52-byte gate stub, the 24-byte seq stub and the three patched code words
    (mixed order is fine - every expected instruction is looked up by address).
    """
    expected = expected_childlock_disassembly()
    by_addr = {}
    lines = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            raw = raw.rstrip("\n")
            if raw and not raw.startswith("#"):
                lines.append(raw)
    for line in lines:
        parts = line.split("\t")
        if len(parts) >= 4 and parts[0].strip().lower().startswith("0x"):
            try:
                by_addr[int(parts[0], 16)] = parts
            except ValueError:
                continue

    failures: List[str] = []
    for addr, mnem, text, raw in expected:
        match = by_addr.get(addr)
        if match is None:
            failures.append(f"instruction at 0x{addr:08X} was not disassembled")
            continue
        d_mnem, d_text = match[1].strip(), match[2].strip()
        d_bytes = match[3].strip().lower()
        if d_mnem != mnem:
            failures.append(f"0x{addr:08X}: mnemonic {d_mnem!r} != {mnem!r}")
        if d_text != text:
            failures.append(f"0x{addr:08X}: text {d_text!r} != {text!r}")
        if d_bytes != raw.hex():
            failures.append(f"0x{addr:08X}: bytes {d_bytes} != assembled {raw.hex()}")

    if failures:
        print("ROUND-TRIP FAILED (child-lock):", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    print(f"round-trip OK (child-lock): {len(expected)} instruction(s) match "
          "byte-for-byte and decode to the intended mnemonic+operands:")
    for addr, mnem, text, raw in expected:
        print(f"  0x{addr:08X}  {raw.hex()}  {text}")
    return 0


def verify_roundtrip(path: str) -> int:
    """Check a Ghidra disassembly dump of the frame stub against the assembly.

    Accepts TSV lines: ``0xADDR<TAB>mnemonic<TAB>text<TAB>hexbytes`` (the format
    written by tools/DumpShim.java in a throwaway Ghidra project).
    """
    expected = expected_disassembly()
    lines = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            raw = raw.rstrip("\n")
            if raw and not raw.startswith("#"):
                lines.append(raw)

    failures: List[str] = []
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
        d_mnem, d_text = parts[1].strip(), parts[2].strip()
        d_bytes = parts[3].strip().lower()
        if d_addr != addr:
            failures.append(f"#{idx}: address 0x{d_addr:08X} != 0x{addr:08X}")
        if d_mnem != mnem:
            failures.append(f"#{idx} 0x{addr:08X}: mnemonic {d_mnem!r} != {mnem!r}")
        if d_text != text:
            failures.append(f"#{idx} 0x{addr:08X}: text {d_text!r} != {text!r}")
        if d_bytes != raw.hex():
            failures.append(f"#{idx} 0x{addr:08X}: bytes {d_bytes} != assembled {raw.hex()}")

    if failures:
        print("ROUND-TRIP FAILED (unlock frame stub):", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    print(f"round-trip OK (unlock frame stub): {len(expected)} instruction(s) match "
          "byte-for-byte and decode to the intended mnemonic+operands:")
    for addr, mnem, text, raw in expected:
        print(f"  0x{addr:08X}  {raw.hex()}  {text}")
    return 0


def _cmd_selftest(_args: argparse.Namespace) -> int:
    return selftest()


def verify_hoist_roundtrip(path: str) -> int:
    """Check a Ghidra disassembly dump of the hoist stub + code patch.

    ``path`` holds TSV lines ``0xADDR<TAB>mnemonic<TAB>text<TAB>hexbytes`` for
    the stub (36 B at HOIST_STUB_ADDR) followed by the patched hook entry
    (4 B at PER_FRAME_HOOK_ADDR) - exactly what
    ``tools/verify_unlock_ghidra.sh --hoist`` dumps.
    """
    expected = expected_hoist_disassembly()
    lines = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            raw = raw.rstrip("\n")
            if raw and not raw.startswith("#"):
                lines.append(raw)

    failures: List[str] = []
    if len(lines) < len(expected):
        failures.append(
            f"disassembly has {len(lines)} instructions, expected {len(expected)}"
        )
    for idx, (addr, mnem, text, raw) in enumerate(expected):
        # The Ghidra dump may contain other regions first; find our addresses.
        match = None
        for line in lines:
            parts = line.split("\t")
            if len(parts) >= 4 and parts[0].strip().lower().startswith("0x"):
                try:
                    if int(parts[0], 16) == addr:
                        match = parts
                        break
                except ValueError:
                    continue
        if match is None:
            failures.append(f"instruction at 0x{addr:08X} was not disassembled")
            continue
        d_mnem, d_text = match[1].strip(), match[2].strip()
        d_bytes = match[3].strip().lower()
        if d_mnem != mnem:
            failures.append(f"0x{addr:08X}: mnemonic {d_mnem!r} != {mnem!r}")
        if d_text != text:
            failures.append(f"0x{addr:08X}: text {d_text!r} != {text!r}")
        if d_bytes != raw.hex():
            failures.append(f"0x{addr:08X}: bytes {d_bytes} != assembled {raw.hex()}")

    if failures:
        print("ROUND-TRIP FAILED (hoist):", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    print(f"round-trip OK (hoist): {len(expected)} instruction(s) match "
          "byte-for-byte and decode to the intended mnemonic+operands:")
    for addr, mnem, text, raw in expected:
        print(f"  0x{addr:08X}  {raw.hex()}  {text}")
    return 0


def _cmd_verify(args: argparse.Namespace) -> int:
    return verify_roundtrip(args.tsv)


def _cmd_verify_hoist(args: argparse.Namespace) -> int:
    return verify_hoist_roundtrip(args.tsv)


def _cmd_verify_childlock(args: argparse.Namespace) -> int:
    return verify_childlock_roundtrip(args.tsv)


def _cmd_blob(args: argparse.Namespace) -> int:
    kinds = {
        "stub": (FRAME_STUB_BYTES, FRAME_STUB_ADDR),
        "hoist-stub": (HOIST_STUB_BYTES, HOIST_STUB_ADDR),
        "hoist-code": (CODE_PATCH_BYTES, PER_FRAME_HOOK_ADDR),
        "gate-stub": (GATE_STUB_BYTES, GATE_STUB_ADDR),
        "seq-stub": (SEQ_STUB_BYTES, SEQ_STUB_ADDR),
        "boot-code": (BOOT_MODE_CODE_BYTES, BOOT_MODE_WORD_ADDR),
        "gate-code": (MENU_GATE_HIJACK_BYTES, MENU_GATE_HANDLER_ADDR),
        "up-code": (UP_HIJACK_BYTES, UP_HANDLER_ADDR),
    }
    if args.kind not in kinds:
        raise SystemExit(
            "--kind must be one of " + ", ".join(sorted(kinds))
            + " (records come from the device)"
        )
    data, base = kinds[args.kind]
    with open(args.out, "wb") as fh:
        fh.write(data)
    print(f"wrote {len(data)} bytes to {args.out}")
    print(hexdump(data, base))
    return 0


def _cmd_plan(args: argparse.Namespace) -> int:
    with open(args.rec, "rb") as fh:
        stock = fh.read()
    forbidden_words = {struct.pack("<I", f) for f in FORBIDDEN_FUNCTIONS}
    for label, addr, blob in plan(stock):
        for off in range(0, max(0, len(blob) - 3)):
            assert blob[off:off + 4] not in forbidden_words, (
                f"{label} embeds a forbidden flash routine at +0x{off:X}"
            )
        print(f"{label:20s} 0x{addr:08X}  {len(blob):4d} B  {blob.hex()}")
    return 0


def _cmd_plan_hoist(_args: argparse.Namespace) -> int:
    for label, addr, blob in hoist_plan():
        kind = "RAM " if _in_ram(addr, len(blob)) else "CODE"
        print(f"{label:12s} {kind} 0x{addr:08X}  {len(blob):4d} B  {blob.hex()}")
    print("\ncode patch site (exact allowlist):")
    for site, length, stock, label in CODE_PATCH_SITES:
        print(f"  0x{site:08X}+{length}  stock {stock.hex()}  {label}")
    return 0


def _cmd_plan_persistent(args: argparse.Namespace) -> int:
    """Offline: print the PERSISTENT (flash-image) recipe as file offsets.

    Recipe only - this never builds, writes or flashes an image.  The recipe
    needs the two trampolines to live in the image itself (the RAM stubs of
    ``child-lock`` do not survive a reboot); the documented host is the 5672-byte
    all-zero linker pad at file 0x07EC40..0x080268 (CPU 0x0207C640..0x0207DC68),
    which no 4-byte word in the stock image points into.
    """
    IMAGE_OFF = 0x2600
    f = lambda cpu: cpu - 0x02000000 + IMAGE_OFF
    gate = build_gate_stub(PERSISTENT_GATE_STUB_ADDR)
    seq = build_seq_stub(PERSISTENT_SEQ_STUB_ADDR)
    if args.image:
        with open(args.image, "rb") as fh:
            blob = fh.read()
        lo = f(PERSISTENT_GATE_STUB_ADDR)
        hi = f(PERSISTENT_SEQ_STUB_ADDR) + len(seq)
        zone = blob[f(PERSISTENT_REGION_LO):f(PERSISTENT_REGION_HI)]
        if set(zone) <= {0}:
            print(f"host region: file 0x{f(PERSISTENT_REGION_LO):06X}.."
                  f"0x{f(PERSISTENT_REGION_HI):06X} is all-zero in {args.image}")
        else:
            print("host region: NOT all zero any more - re-verify before using")
        if any(blob[o:o + 4] != bytes(4) for o in range(lo, hi, 4)):
            raise SystemExit("the host region is not free in this image")
    print(f"image offset map: file = CPU - 0x02000000 + 0x{IMAGE_OFF:X}")
    print("\nthe three patch words:")
    for site, _l, stock, _patched, label in CHILDLOCK_WORD_SITES:
        if site == BOOT_MODE_WORD_ADDR:
            patched = BOOT_MODE_CODE_BYTES
        elif site == MENU_GATE_HANDLER_ADDR:
            patched = enc_lj(site, PERSISTENT_GATE_STUB_ADDR)
        else:
            patched = enc_lj(site, PERSISTENT_SEQ_STUB_ADDR)
        print(f"  file 0x{f(site):06X} (CPU 0x{site:08X})  {stock.hex()} -> "
              f"{patched.hex()}   {label}")
    print("\nthe two trampolines (their two l.j words are position dependent):")
    for name, addr, data in (("gate", PERSISTENT_GATE_STUB_ADDR, gate),
                             ("seq ", PERSISTENT_SEQ_STUB_ADDR, seq)):
        print(f"  {name} stub: file 0x{f(addr):06X} (CPU 0x{addr:08X}), {len(data)} B")
        for i in range(0, len(data), 4):
            print(f"    0x{f(addr + i):06X}  {data[i:i + 4].hex()}")
    print("\nflag bytes stay in RAM (never part of an image): "
          f"0x{CHILDLOCK_SEQ_ADDR:08X} / 0x{CHILDLOCK_FLAG_ADDR:08X}")
    return 0


def _cmd_stock_check(args: argparse.Namespace) -> int:
    """Read-only check of an offline firmware copy against every embedded
    'stock bytes' expectation, plus the mode-3 hook-list binding of the UP
    handler.  Never writes anything; the image is opened read-only.

    CPU = 0x02000000 + file_offset - 0x2600  (CONFIRMED: app code at 0x02000000
    lives at file offset 0x2600).
    """
    IMAGE_OFF = 0x2600
    failures: List[str] = []
    with open(args.image, "rb") as fh:
        blob = fh.read()

    def read(cpu: int, length: int) -> bytes:
        off = cpu - 0x02000000 + IMAGE_OFF
        if off < 0 or off + length > len(blob):
            raise SystemExit(f"0x{cpu:08X} is outside {args.image}")
        return blob[off:off + length]

    print(f"image : {args.image} ({len(blob)} bytes)")
    got_md5 = hashlib.md5(blob).hexdigest()
    if got_md5 == FIRMWARE_MD5:
        print(f"md5   : {got_md5}  (the documented original)")
    else:
        print(f"md5   : {got_md5}  != documented {FIRMWARE_MD5} "
              "(different/patched image - the stock-byte check below still governs)")
    print("code patch sites:")
    for site, length, stock, label in CODE_PATCH_SITES:
        got = read(site, length)
        ok = got == stock
        if not ok:
            failures.append(f"0x{site:08X} holds {got.hex()}, expected {stock.hex()}")
        print(f"  {'OK  ' if ok else 'FAIL'} 0x{site:08X} "
              f"(file 0x{site - 0x02000000 + IMAGE_OFF:X}) "
              f"{got.hex()} {label}")

    print("child-lock patch words:")
    for site, length, stock, patched, label in CHILDLOCK_WORD_SITES:
        print(f"  0x{site:08X} (file 0x{site - 0x02000000 + IMAGE_OFF:X})"
              f"  {stock.hex()} -> {patched.hex()}   {label}")

    print("mode-3 hook list bindings:")
    for i in range(0, 32):
        pair = read(MODE3_HOOK_LIST + 8 * i, 8)
        key, handler = struct.unpack("<II", pair)
        if key >= 0x2E or handler == 0:
            break
        if key in (0x1B, 0x24):
            expect = UP_HANDLER_ADDR if key == 0x1B else MENU_GATE_HANDLER_ADDR
            name = "UP" if key == 0x1B else "POWER"
            ok = handler == expect
            if not ok:
                failures.append(
                    f"hook list id 0x{key:02X} -> 0x{handler:08X}, expected 0x{expect:08X}"
                )
            print(f"  {'OK  ' if ok else 'FAIL'} id 0x{key:02X} ({name:5s}) -> "
                  f"0x{handler:08X}  (handler word at 0x{MODE3_HOOK_LIST + 8 * i + 4:08X})")

    print("ui_app_task initial-mode select:")
    boot = read(BOOT_MODE_WORD_ADDR - 0x18, 0x28)
    print("  0x02000494..0x020004BB: " + boot.hex(" "))

    if failures:
        print("\nSTOCK CHECK FAILED:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    print("\nstock check OK: every embedded stock word matches the image")
    return 0


def _cmd_plan_childlock(_args: argparse.Namespace) -> int:
    """Offline: print every write of the child-lock patch (and its restore)."""
    forbidden_words = {struct.pack("<I", f) for f in FORBIDDEN_FUNCTIONS}
    for header, writes in (("apply", childlock_plan()),
                           ("restore", childlock_restore_plan()),
                           ("live (transient)", childlock_live_plan())):
        print(f"{header}:")
        for label, addr, blob in writes:
            kind = "RAM " if _in_ram(addr, len(blob)) else "CODE"
            if any(blob[off:off + 4] in forbidden_words
                   for off in range(0, max(0, len(blob) - 3))):
                raise SystemExit(f"{label} embeds a forbidden flash routine")
            print(f"  {label:24s} {kind} 0x{addr:08X} {len(blob):4d} B  {blob.hex()}")
    print("\ncode patch sites (exact allowlist):")
    for site, length, stock, label in CODE_PATCH_SITES:
        print(f"  0x{site:08X}+{length}  stock {stock.hex()}  {label}")
    print("\nflag bytes:")
    print(f"  0x{CHILDLOCK_SEQ_ADDR:08X}  CHILDLOCK_SEQ  (set by an UP tap, cleared when used)")
    print(f"  0x{CHILDLOCK_FLAG_ADDR:08X}  CHILDLOCK_FLAG (adult override; child-lock adult-on/off)")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    p_self = sub.add_parser("selftest", help="offline checks of the patch definition")
    p_self.set_defaults(func=_cmd_selftest)

    p_blob = sub.add_parser("blob", help="emit a RAM/code patch blob")
    p_blob.add_argument("--kind",
                        choices=["stub", "hoist-stub", "hoist-code",
                                 "gate-stub", "seq-stub", "boot-code",
                                 "gate-code", "up-code"],
                        default="stub")
    p_blob.add_argument("--out", required=True)
    p_blob.set_defaults(func=_cmd_blob)

    p_plan = sub.add_parser("plan", help="print the patch for a stock-record dump")
    p_plan.add_argument("--rec", required=True,
                        help="file holding the 0x100-byte stock Main-menu record")
    p_plan.set_defaults(func=_cmd_plan)

    p_ph = sub.add_parser("plan-hoist",
                          help="print the per-frame USB/BOT hoist patch (RAM stub "
                               "+ one code write)")
    p_ph.set_defaults(func=_cmd_plan_hoist)

    p_pc = sub.add_parser("plan-childlock",
                          help="print the offline child-lock patch: two RAM stubs, "
                               "two flag bytes, three code words, the transient "
                               "live jump and the restore plan")
    p_pc.set_defaults(func=_cmd_plan_childlock)

    p_sc = sub.add_parser("stock-check",
                          help="offline: verify a firmware image copy against every "
                               "embedded stock-byte expectation (read-only)")
    p_sc.add_argument("--image", required=True,
                      help="path to an original_firmware_backup.bin copy")
    p_sc.set_defaults(func=_cmd_stock_check)

    p_pp = sub.add_parser("plan-persistent",
                          help="offline RECIPE: print the flash-image variant as file "
                               "offsets (never builds or flashes an image)")
    p_pp.add_argument("--image", default=None,
                      help="optional: verify the stub host region is still free")
    p_pp.set_defaults(func=_cmd_plan_persistent)

    p_verify = sub.add_parser("verify-roundtrip",
                              help="check a Ghidra dump of the frame stub")
    p_verify.add_argument("tsv", help="TSV written by DumpShim.java")
    p_verify.set_defaults(func=_cmd_verify)

    p_vh = sub.add_parser("verify-hoist-roundtrip",
                          help="check a Ghidra dump of the hoist stub + code patch")
    p_vh.add_argument("tsv", help="TSV written by DumpShim.java")
    p_vh.set_defaults(func=_cmd_verify_hoist)

    p_vc = sub.add_parser("verify-childlock-roundtrip",
                          help="check a Ghidra dump of both child-lock stubs + the "
                               "three patched code words")
    p_vc.add_argument("tsv", help="TSV written by DumpShim.java")
    p_vc.set_defaults(func=_cmd_verify_childlock)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
