# KC02 child lock — PERSISTENT firmware-patch recipe (recipe only)

**Status: RECIPE, NOT IMPLEMENTED.**  Nothing in this repository builds a
firmware image, and no tool here writes flash.  This document exists so that the
RAM-poke patch implemented by `tools/kc02_usb.py child-lock` can later be turned
into a permanent modification by someone holding the device, a full 4 MB dump of
it, and a way to program the SPI flash (soldering/clip + CH341A-style programmer
or the vendor's own tooling).

Firmware this recipe was derived from:
`original_firmware_backup.bin`, md5 `7236c4c1bd3f12b1d85d33a9bebdf510`
(the dump the whole project's addresses were verified against).

Standing rules for this repo (unchanged):

* The five flash-modifying routines (`0x0203D584`, `0x0203D454`, `0x0203D4E8`,
  `0x0203D25C`, `0x02002418`) must stay unreachable from every tool and patch.
  Nothing in this recipe calls them.
* The RAM-poke path (`poke` / `unlock-usb` / `usb-hoist` / `child-lock`) is the
  only supported way to apply the patch to a running camera.  This file is the
  fallback for a camera that has to keep the lock without a host script.

Everything below is reproducible offline:

```sh
tools/kc02_unlock.py stock-check   --image original_firmware_backup.bin   # stock bytes + hook-list binding
tools/kc02_unlock.py plan-persistent --image original_firmware_backup.bin # the offsets/bytes in §1..§2
tools/verify_childlock_ghidra.sh                                          # disassembly round-trip of the stubs + words
```

## 0. Address → file-offset map

```
file_offset = CPU - 0x02000000 + 0x2600
```

CONFIRMED: app code at CPU `0x02000000` is at file offset `0x2600`.

## 1. The three patch words

| # | file offset | CPU | old bytes | new bytes | meaning |
|---|---|---|---|---|---|
| 1 | `0x002AAC` | `0x020004AC` | `06 00 60 9c` | `03 00 60 9c` | `b.addi r3,r0,0x6` → `b.addi r3,r0,0x3`: boot into the camera (mode 3) instead of the Main menu (mode 6) |
| 2 | `0x00C508` | `0x02009F08` | `fc 4f e1 d7` | `ce c9 01 00` | `b.sw -0x4(r1),r9` → `l.j 0x0207C640`: hijack the camera POWER handler entry into the adult gate stub |
| 3 | `0x00BE90` | `0x02009890` | `f4 97 e1 d7` | `7c cb 01 00` | `b.sw -0xc(r1),r18` → `l.j 0x0207C680`: hijack the mode-3 UP handler entry into the arm-the-sequence stub |

Word 1 is self-contained: it needs no stub and no RAM.

Words 2 and 3 must point at code that exists **in the image** (see §2).  The
words above are the recipe's own values, i.e. the stubs hosted at CPU
`0x0207C640` / `0x0207C680`.  If you pick a different host address, the two
`l.j` words must be re-encoded (they are PC-relative):
`word = (delta >> 2) & 0x03FFFFFF`, little-endian, `delta = target - instruction`.

## 2. The two trampolines (must be part of the image)

The RAM variant writes these two stubs into RAM at `0x020D9400` / `0x020D9440`.
Those addresses are BSS: the boot code initialises them, so an image that only
contains the three words above would jump into RAM that is *not* the stub and
the camera would hang or misbehave.  For a persistent patch the two stubs must
therefore be stored **in the image, at an executable address inside the app's
XIP window**.

Host region used here (free in the stock image, and *only* in the stock image —
re-verify before use):

```
file 0x07EC40 .. 0x080268 = CPU 0x0207C640 .. 0x0207DC68  5672 all-zero bytes
```

Evidence it is padding: a 5672-byte run of `0x00` between two rodata tables
(file `0x07EC40` is preceded by a small byte-permutation table, file `0x080268`
is followed by a run of `0x00000001` words), and **no** word-aligned 4-byte word
anywhere in the stock image equals an address inside that range.  It is a linker
pad, not data.  It is inside the app image window (`0x02000000`-based XIP), so it
is fetched like any other app code.  `plan-persistent --image` re-checks both
facts.

Placement (52 B + 24 B, both word-aligned; the rest of the pad stays zero):

```
gate stub — CPU 0x0207C640, file 0x07EC40
  0x07EC40  0d 02 c0 18   b.movhi r6,0x20d
  0x07EC44  80 94 c6 a8   b.ori   r6,r6,0x9480   ; r6 = 0x020d9480 (RAM flag bytes)
  0x07EC48  00 00 e6 8c   b.lbz   r7,0x0(r6)     ; r7 = CHILDLOCK_SEQ
  0x07EC4C  00 00 27 bc   b.sfnei r7,0x0         ; F = (SEQ != 0)
  0x07EC50  03 00 00 0c   b.bnf   0x0207c65c     ; not armed -> check the adult flag
  0x07EC54  00 00 06 d8   b.sb    0x0(r6),r0     ; consume the arm (one shot)
  0x07EC58  04 00 00 00   l.j     0x0207c668
  0x07EC5C  01 00 e6 8c   b.lbz   r7,0x1(r6)     ; CHILDLOCK_FLAG
  0x07EC60  00 00 27 bc   b.sfnei r7,0x0
  0x07EC64  03 00 00 0c   b.bnf   0x0207c670     ; not the adult -> ignore the event
  0x07EC68  fc 4f e1 d7   b.sw    -0x4(r1),r9    ; re-execute the displaced stock instr
  0x07EC6C  28 36 fe 03   l.j     0x02009f0c     ; resume the stock POWER handler
  0x07EC70  00 48 00 44   l.jr    r9             ; fail: clean ignore, no stack change

seq stub — CPU 0x0207C680, file 0x07EC80
  0x07EC80  0d 02 c0 18   b.movhi r6,0x20d
  0x07EC84  80 94 c6 a8   b.ori   r6,r6,0x9480
  0x07EC88  01 00 e0 9c   b.addi  r7,r0,0x1
  0x07EC8C  00 38 06 d8   b.sb    0x0(r6),r7     ; arm CHILDLOCK_SEQ = 1
  0x07EC90  f4 97 e1 d7   b.sw    -0xc(r1),r18   ; re-execute the displaced stock instr
  0x07EC94  80 34 fe 03   l.j     0x02009894     ; resume the stock UP handler
```

The two `l.j` words inside the stubs are the only position-dependent bytes; the
two `b.bnf` words are PC-relative to the stub itself and are identical to the
RAM variant.  The register discipline is the same as the RAM variant: `r3/r4/r5`
(payload, flag, `&ev`) and `r9` are preserved on every path, `r1` is never
touched, only `r6/r7` are used.

The two control bytes stay in RAM and are **not** part of the image:

```
0x020D9480  CHILDLOCK_SEQ   set by a UP tap, cleared when it lets one POWER through
0x020D9481  CHILDLOCK_FLAG  adult override: 1 = the menu opens on a single POWER press
```

Behaviour after flashing: power-on boots straight into the camera; the Main
menu needs **UP, then POWER**; `child-lock adult-on` (a plain RAM poke) makes it
a single POWER press until the next power-off.

## 3. Canary-first rule (standing rule — apply it before the real patch)

Do **not** make the child-lock patch the first thing you write to flash.  Prove
the write path, the address map and your dump/compare procedure with a
**same-length, harmless** change first:

1. Pick the version string `329X_V1.0.0` at **file `0x7DF9F`** (11 ASCII bytes +
   NUL, CPU `0x0207DF9F`; the string sits directly after `"exmend.bin"`).
2. Change it to another **11-character** string, e.g. `329X_V1.0.1`
   (`33 32 39 58 5F 56 31 2E 30 2E 30` → `33 32 39 58 5F 56 31 2E 30 2E 31`).
   Same length ⇒ no offsets move, no checksum/size field changes.
3. Flash that, power-cycle.  Verify the camera still boots and behaves normally
   (the string is reported/printed by some paths, so you can also confirm the
   change took effect).
4. Only after the canary boots cleanly: apply §2 (stubs, 76 bytes of otherwise
   zero padding) and then §1 (the three words) in one or two writes, keeping a
   byte-exact copy of your read-back.
5. Read the flash back after every write and diff it against the intended image
   before you trust the device (`cmp`/`md5sum`); a single-byte mismatch means
   re-do the write, not "it is probably fine".

## 4. Recovery

* Keep the untouched `original_firmware_backup.bin`
  (md5 `7236c4c1bd3f12b1d85d33a9bebdf510`) **and** a fresh read-back of your own
  chip.  They are not necessarily identical: a dump contains OTP/config areas and
  the exact read-back is what you need to restore your chip.
* Recovery that always works: **desolder the SPI flash (or lift it with a clip),
  program it externally with the original image, and re-fit it.**  This does not
  depend on the camera's own boot loader, on USB, or on anything the patch did.
* Do **not** attempt recovery through the camera's own USB/BOT transport unless
  you have already proven that path works (that is exactly what the canary in
  §3 is for).  The app's own update path — `fw_upgrade_task_DANGEROUS_erase_program`
  `0x02002418` and the SPI erase/program routines — stays out of scope here and
  out of reach of every tool in this repo.
* If the camera boots but behaves oddly, first re-read the flash and diff against
  the intended image; a partially programmed sector is the usual cause.

## 5. What is NOT covered here

* No image is built, patched, signed, checksummed or flashed by this repository.
* The `0xCD`/BOT transport write path is unaffected by this recipe and must not
  be used to write flash.
* The boot word (§1, row 1) and the two hijack words (§1, rows 2–3) are all the
  image needs *besides* the trampolines in §2; the RAM flag bytes are runtime
  state.
* Residual risk: the "free" pad at file `0x07EC40` is free **in this dump**.
  Re-verify it (`plan-persistent --image`) before every use, and re-verify the
  stock bytes at all three word sites (`stock-check`) — a different firmware
  build will move them.
