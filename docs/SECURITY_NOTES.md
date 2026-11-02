# Security notes: card-supplied input on the AX3292 KC02 family

Working notes from a vulnerability hunt on the three dumped builds:

| build | file | sha256 |
|---|---|---|
| original KC02 | `firmware/original.bin` | `218c6cdf…21a5` |
| MYSTILUCK X8D | `firmware/mystiluck-x8d-8e0636e1.bin` | see `README` |
| HMOLLIMT Z-12 | `firmware/hmollimt-z12-10da5dbf.bin` | see `README` |

Scope: **no teardown**. Every vector had to be reachable through user-supplied
data alone — a FAT card (or the same card exposed over USB mass storage) and the
update file. The goal was code execution to read SPI flash (and RAM) from the
device, on as many of the builds as possible.

Everything below was established against the emulator
(`tools/emu/run.py`), which runs the stock firmware instruction-for-instruction.
No hardware was written.

## Bottom line

* **The vendor command path is model-dependent and closed on the target.**
  `0xCD` works on `0219:3280` but is provably absent on `349c:3301` (control-
  verified: `0xCB` answers with byte-identical sense data to a deliberately
  bogus opcode). See “Closed: the vendor command path” — do not reopen it. Any
  model-agnostic work must assume no vendor opcode exists.
* **The AVI exploit is addressed, not portable.** It needs the video buffer
  base, and that base moves with build/BSS size (the three dumps differ by
  4 MB and 6 MB in `stack_top` alone). Making it work on an unknown model is an
  address-discovery problem first and an exploit problem second.
* **The updater is the second card-only code-execution path**: it is
  unauthenticated by design (no signature, no CRC, no rollback) and overwrites
  the bootloader too. Present in the original and X8D images; the Z-12's update
  path differs and is uncharacterised.
* **Card-only code execution is demonstrated: the AVI `idx1` read overflow.**
  A crafted `VIDEO/MOV00001.AVI` gives arbitrary-length heap-and-stack overwrite
  from a single `f_read`, and a 12-byte trampoline planted in the overwritten
  region hijacks control. Verified in the emulator by having the injected code
  copy the whole 812 KB application image into RAM, 99.8% byte-exact against
  the dump and 540 KB of it perfectly (see “Finding 3”).
  Success is a race: 11/22 runs, about 50%.
* **The other reachable parsers held.** FatFs R0.13a, the Helix-lineage MP3
  decoder, and the JPEG header parser behind the photo viewer survived targeted
  and mutated input; the retracted hypothesis below is worth as much as the
  findings because it closes off an avenue. The WAV/ADPCM route from the SDK
  source is dead: the decoder is not linked into any of the three images.


## Closed: the vendor command path (`0xCD` / `0xCB`)

Do not reopen this. It is settled by measurement, on two models, and one of them
has the block while the other provably does not.

`0xCD` is the stock "PC tools" vendor opcode: CDB `0xCD` + three little-endian
words (routine, buffer, callback) plus a 3-byte offset, executed with the callback
skipped. `docs/hw/` (`dump_firmware.py`, `audit_image.py`, `observe_state.py`,
`execute_payload.py`) drives it to read memory and run payloads.

| model | vendor block | evidence |
|---|---|---|
| `0219:3280` (KC02 lineage) | **present** | `0xCD` reads and executes; `docs/hw/` tooling |
| `349c:3301` (rev 3.10, serial `20210901000000`) | **absent** | control experiment below |

### The control experiment (this is what makes it conclusive)

Sent through the MSC interface via `SG_IO` (`/tmp/vendtest.py`):

```
0x00 TEST UNIT READY: status=0 host=0 driver=0 resid=0
   => command ACCEPTED by the device
0x12 INQUIRY: status=0 host=0 driver=0 resid=0
   data: 008004022d00000047656e65726963204d6173732d53746f7261676520202020
   => command ACCEPTED by the device
0xEE bogus opcode: status=2 host=0 driver=8 resid=0
   sense: 700005000000000a00000000240000000000
   sense key=0x5 asc=0x24 ascq=0x00
   => CHECK CONDITION (rejected / vendor opcode not handled)
0xCB sub=0x00: status=2 host=0 driver=8 resid=0
   sense: 700005000000000a00000000240000000000
   sense key=0x5 asc=0x24 ascq=0x00
   => CHECK CONDITION (rejected / vendor opcode not handled)
```

`0xEE` is a deliberately invalid opcode; it is what "unknown opcode" looks like on
this build. `0xCB` produces a **byte-identical** sense response, so the dispatch
ran and had no case for it. In the SDK, `scsi_cmd_analysis` falls through to
`mscSet_Status(INVALID_FIELD_IN_COMMAND)` + `ScsiStallIn` for unhandled opcodes,
which is exactly key `0x5` / ASC `0x24`. The working `0x00`/`0x12` results remove
"my packet was malformed" as an alternative explanation.

Consequence: there is no vendor readout and no vendor call primitive on this
build, so a **card-only** path is the only route.

### Related closure: no second LUN

BOT-level probe (raw CBW/CSW over bulk endpoints, `bCBWLUN` 0-3): all LUNs return
the same INQUIRY (`Generic` / `Mass-Storage` / `3.10`) and the same capacity
(`last_lba = 61439999`, 512 B = 31.4 GB). The firmware ignores the LUN field, so
there is no hidden LUN exposing SPI flash.

### What this implies for the goal

The objective is a vector that works across as many models as possible, **not** one
that depends on a vendor opcode, because the opcode is exactly what varies between
models. Card-supplied media parsing is the surface that stays constant — audio
playback from the card in particular is a feature present on essentially every
model in this family, which is why the media parsers are the right hunting ground.

## Method and tooling

The stock QEMU machine is a good harness because it is **bit-exact
deterministic**: two runs of the same card differ in 0 bytes across the decoder
BSS, the app-state region and the heap guard (verified). That makes a
snapshot-diff oracle usable.

Scratch tooling used for this hunt (not yet in the repo):

| tool | purpose |
|---|---|
| `mkcard.py` | generic FAT16 card builder: arbitrary directories/files, 8.3 names. Replaces the photo-only seeding in `create_sd.py`. |
| `probe.py` | boot, drive the UI with real QMP key events, screenshot, read firmware memory (`pmemsave`). |
| `fuzz.py` | per-case boot → open media → snapshot decoder/app/heap regions → diff. |
| `mutate.py` | parallel MP3 mutation runner. |
| `jfuzz.py` | parallel JPEG mutation runner. |
| `afuzz.py` | parallel AVI mutation runner. |
| `mkavi.py` | minimal accepted MJPEG AVI builder (`hdrl`/`avih`, indexed `MJPG`, `idx1`). |
| `tramp.py` | AVI `idx1` overflow exploit: payload build, run, flash-copy verification. |

Two traps worth recording:

* **BPB consistency matters.** A card whose BPB declares 32 root entries but
  reserves 32 sectors for the root directory mounts as FAT16 and then reads
  nonsense: FatFs derives the data-area start from `RootEntCnt`, so every cluster
  is off by 30 sectors. Files "exist" but are never found. Use
  `RootEntCnt = 512` with a 32-sector root area.
* **A generic MJPEG AVI does not engage the video decoder.** See the AVI section.

## Finding 1 — the SD updater performs no validation at all (universal, by design)

`sd_update_boot` (`0x02002418`) is reached at boot when a card is present and
`app_context.b4` signals a card:

1. `fs_mount`, open `DestBin.bin` from the card root;
2. `hal_spiFlashEraseChip()` — erases the **whole** 4 MB chip, bootloader included;
3. program the file from offset `0x200` in `0x1000` chunks, then write the first
   `0x200` bytes;
4. verify by reading back `0x800`-byte chunks and comparing.

There is no signature, no CRC over the image, no version/anti-rollback check and
no size limit. The checks that documentation elsewhere attributes to the updater
(`BLDR`, `0x55AA`, `0x01234567`, `SFAT`, additive checksum) are **not** enforced
by this routine; whatever the card contains is what lands in flash.

Consequence: anyone who can hand the camera a card gets persistent arbitrary code
execution at the next power-on, and can read/write the entire flash and RAM from
that code. It is also an unrecoverable brick if the image is bad, because the
bootloader is erased too.

Build coverage: `DestBin.bin` is present in the original and X8D images. The
Z-12 has no such literal and gains a `CRC check failed` string, so its update
trigger and validation are **different and not yet characterised** — do not
assume this finding transfers to it.

## Finding 2 — media player deletes files it cannot play

`photo_show_file` (`0x0200acbc`) opens the selected media file, and on a failed
`video_playback_start` calls `f_unlink(p0)` before continuing. A malformed or
unsupported file placed in `VIDEO/` is therefore silently deleted by the camera.
Low severity (data loss of attacker-chosen files only), but it is a destructive
action taken on unvalidated input.

## Finding 3 — card-only code execution: AVI `idx1` read overflow

The only memory-corruption path found, and it yields PC control from a single
file on the card.

### The primitive

`playback_decode_service` (`0x02053818`) services the `idx1` index queue. For a
video chunk it performs

```c
read(fd, video_buf, idx1.dwChunkLength + 8);   /* no clamp */
```

`video_buf` is a 1 MB heap allocation at `0x02368200`. The **audio** path clamps
its length through `DAT_ram_020d1d34`; the video path does not. The read is one
contiguous fill, so the overflow is not confined to that buffer — it walks up the
whole heap and into the stack at `0x027FEC00–0x02800000`.

Measured delivery law, stable across all runs:

```
delivered = min(dwChunkLength + 8 - (movi_pos + dwChunkOffset), file_size)
```

so the file only needs to be as long as the reached extent, and the read always
starts at `movi_pos + dwChunkOffset` whatever the chunk headers claim.

### Reachability trap

`namemake` (`0x0200160c`) admits only `MOV`, `MIC` and `LOK` filename prefixes;
anything else returns `0x40000000` and is dropped by `fs_filelist_add`, so the
player never opens it. The file must be named e.g. `VIDEO/MOV00001.AVI`. A
generic MJPEG AVI also does not engage the path — it needs `hdrl`/`avih`, indexed
`MJPG` records inside `movi`, and an `idx1`.

### What breaks, and why

Naively spraying the whole extent kills the machine instead of helping, because
the copy marches through frames that are *live* while it runs. Two constraints
had to be found empirically:

* **Live locals must be preserved.** In the `f_read` frame, `0x27FFEEC` and
  `0x27FFEF0` hold the `FATFS*` (`0x02103720`), and `0x27FFF20` / `0x27FFF24` /
  `0x27FFF28` hold the remaining count, the destination end pointer, and a
  playback global. Overwriting the destination pointer makes `f_read` store
  through an attacker-derived address — observed as `PC=0xc2402888`,
  `R01=0x18804b43`, i.e. stub *instruction words* used as data. Preserving those
  five slots lets the full 4.8 MB cross-stack write complete with no fault.
* **Entry must survive a pop from inside the stub region.** The hijack restores
  `r1` from a clobbered slot to the sprayed stub address, then loads the return
  address from an offset around `+0x2c…+0x50` *inside* that region. A stub longer
  than that window leaves instruction words there and jumps into garbage. The
  layout that works is a 12-byte trampoline at `VIDBUF+0x2000` (`0x0236a200`:
  `l.movhi r9,0x237; l.jr r9; l.nop`) with the real payload at `0x02370000`.

The sprayed value must also be *mapped* RAM: `0x0236a200` dereferences cleanly,
whereas `0x11111111` (unmapped) aborts the read early.

### Verified result

Two payloads were used. The first (256 B) proved the primitive; the second reads
the **entire application image**:

```
0x02000000 .. 0x020C6200  (811,520 bytes)  ->  0x03000000
```

Result in the emulator, comparing the RAM copy against `firmware/original.bin`
at file offset `0x2600`:

```
len=811520  total_mismatch=1606/811520   (99.8% byte-identical)
mismatched 4 KB blocks: 5 of 198
   0x083000:   48 bytes   0x0C2000:  32 bytes
   0x084000: 1520 bytes   0x0C3000:   2 bytes
                          0x0C4000:   4 bytes
```

Everything below `0x083000` — 540,672 bytes of code — is **byte-exact**. The
residue is live mutable data (`0x02083xxx`/`0x02084xxx` working state and the
`0x020C2xxx–0x020C5xxx` block at the end of the image), i.e. the difference
between a running device and a static dump, not a read error. Some runs write the
entry marker but are knocked back into app code before finishing, so **completion
is judged on the copy, not the marker.**

### Making the copy actually finish

The first full-image attempts all stopped after roughly 430–540 KB with control
back in app code. The cause is the exception model, not the exploit: the stub was
being preempted onto the *corrupted* stack, so the handler's return went to
attacker-controlled memory. Masking the right `SR` bits fixes it:

```asm
l.mfspr r2, r0, 0x11        # SR
l.andi  r2, r2, 0xfffc      # clear TEE (bit 1) and IEE (bit 2)
l.mtspr r0, r2, 0x11
```

Two traps worth recording:

* `SR` bit 0 is **SM** (supervisor mode), not interrupt enable. Clearing it drops
  the CPU into user mode and the stub dies; observed as a jump to `0x1e1c`.
* Clearing only `IEE` is not enough. `TEE` (bit 1, the tick timer) is a separate
  exception source and stayed enabled — `SR` read back as `0x00008003`. That
  periodic tick is what cut the copy short at a near-constant offset.

With both bits masked the full 812 KB copy completes on every run that gets
control (8/8), where before it never did.

### Why success is only ~50%

The stack band is crossed in every run — sampled at `t≈0.138 s`, all of
`0x27FFE00…0x27FFF34` is overwritten inside a ~2 ms window. But those slots also
hold saved LRs of loops that are *about* to re-push their frames, so unless a
return or interrupt consumes the clobbered LR during the crossing, playback just
continues and later frames restore the real values. Successes cluster at
`t≈0.13 s` for that reason. Putting the giant length on the last `idx1` entry, or
repeating it on several entries, did **not** improve the rate — a failed attempt
is terminal for that playback session.

### Scope across the three dumps

Everything above was verified on **`firmware/original.bin` (KC02)**. Do not read
the other two images as "fixed" — they are different products with different
memory maps, so an address-specific exploit cannot be expected to match:

| image | `stack_top` | app range | app size |
|---|---|---|---|
| original (KC02) | `0x027FFFFC` | `0x02600..0xD3200` | 835 KB |
| x8d (Mystiluck X8D) | `0x023FFFFC` | `0x02200..0x7F000` | 500 KB |
| z12 (Hmollimt Z-12) | `0x021FFFFC` | `0x02600..0x9A600` | 608 KB |

A four-instruction fingerprint of the bug site
(`l.lwz r5,0x8(r1)` / `l.lwz r3,0x78(r14)` / `l.lwz r4,0x0(r18)` /
`l.addi r5,r5,0x8`) matches only the KC02 image. That is **not** evidence the
other builds clamp the length: with control functions from the same SDK the
8-byte-window match is 9–20 of 23, while the playback region scores 2–3 of 23 —
i.e. the media code is materially restructured, not necessarily fixed. Naming any
conclusion here requires either a decompile of those two builds or running the
overflow against them in the emulator (profiles for both already exist in
`tools/emu/profiles.py`).

Porting is mechanical but not free: per build you must re-derive the video buffer
base, the stack band, and the live `f_read` locals being preserved.

Known unknown: whether the unit on the bench runs this build at all. The
`SELFTEST.bin` hidden menu present in `original.bin` produced no visible change on
the live camera, which argues the live build is **not** identical to
`original.bin`.

### The hijack is a *base pointer*, not a jump target — so the stack address is not needed

Retested on the now-deterministic base, setting the stack band to the spray value
plus a deliberate error `delta`:

| delta | band value | result |
|---|---|---|
| `0` | `0x0236A200` | 3/3 SUCCESS |
| `+4` | `0x0236A204` | 3/3 SUCCESS |
| `+0x1C` | `0x0236A21C` | 3/3 SUCCESS |
| `+0x1000` | `0x0236B200` | 3/3 SUCCESS |
| `+0x100000` | `0x0246A200` | 3/3 SUCCESS |
| `+0x200000` | `0x0256A200` | 3/3 SUCCESS |
| `-0x100000` | `0x0226A200` | 3/3 FAIL |

So the band value is **dereferenced as a base pointer**, and the return address is
loaded from `[value + K]` — which is still the uniform spray, so it resolves to the
trampoline for any `delta` that keeps the pointer inside the payload. Negative
`delta` fails because it leaves the payload.

Consequence: **`stack_top` does not have to be known.** A uniform spray fills the
stack band automatically, wherever it is. Only the *sprayed value* still has to
equal a trampoline address (`B + 0x2000`), i.e. it is still exact in `B`. That is
the single remaining unknown, and it is why the blind search size is unchanged.

### Hidden `SELFTEST.bin` menu exists in one image only

Checked all three dumps for the loader's filename literals and the launcher
function:

| image | `SELFTEST` literal | `exmend.bin` literal | launcher windows matching |
|---|---|---|---|
| original (KC02) | present | present | 62/62 |
| x8d | absent | absent | 28/62 |
| z12 | absent | absent | 28/62 |

The filename is compared against the card, so it survives compilation as a
literal: its total absence in x8d/z12 is strong evidence those builds have the
factory menu stripped. The 28/62 match is the surrounding menu framework, not the
launcher.

Note this also means the KC02 `SELFTEST.bin` menu is **not** a portable lever, and
that its failure to appear on the `349c:3301` bench unit is consistent with that
unit running a build without it.

### The hijack chain, fully resolved: `PC = *(V + 0x20)`

Recovered by filling the payload with a unique per-word marker (`0x81000000|offset`)
and putting a chosen pointer `V = B + 0x100000` in the stack band, then reading the
exception log (`-d int`):

```
INT: TICK  pc=81100020  epcr=81100020
```

`0x81100020` decodes to payload offset `0x100020`, so the word fetched was at
`V + 0x20`. The chain is therefore:

```
band slot value V  ->  read *(V + 0x20)  ->  PC = that word
```

Consequences:

* `V` only has to be a **readable address inside the payload** — hence the +/- 2 MB
  tolerance measured earlier, and hence `stack_top` need not be known.
* The word at `V + 0x20` must be a **code pointer**. That single requirement is
  what makes `B` mandatory, and it cannot be removed by spray geometry:

| what we want | conflict |
|---|---|
| spray a jump sled so `PC` anywhere is fine | then `*(V+0x20)` is a *hop instruction*, so `PC` = an instruction word, not an address (this is exactly the observed `PC=0x03FFFFFE`) |
| spray pointers so `*(V+0x20)` is an address | then execution starts on a pointer word, which is not code; and with `K = 0x20` (a multiple of 8) the pointer slot and the slot it reads have the same parity, so the two roles can never be separated by interleaving |

So the blind search over `B` stands: **546,577** candidates at 4-byte granularity,
**34,125** if `B` is always 64-byte aligned (all three dumps are). At ~20 s of
manual menu navigation per attempt that is weeks to months of resets, which is not
a real option — which is why obtaining the image remains the practical route.

### Allocator state, measured (and a corrected node layout)

`hal_memNodeMalloc` (`0x0203e430`) pops from the static 64-node pool at
`0x020cec5c`, free head at `0x020cec58`:

```
l.lwz r11,0x0(r3)      ; head
l.sfeqi r11,0x0
l.bf  <return NULL>    ; empty pool -> returns NULL, no free-list consultation
l.lwz r4,0xc(r11)      ; next  <-- at offset 0xC, NOT 0
l.sw 0x0(r3),r4        ; head = next
```

Measured occupancy: **27/64 free at idle, 32/64 free during AVI playback** (the
player frees more than it holds). A first reading of "1/64" was my own bug — I
walked `next` at offset 0. The pool is therefore *not* near exhaustion, and
exhausting it (the precondition for the unchecked `hal_sysMemMalloc(0x400)` ->
write-at-NULL into the SRAM exception vectors) needs an allocation leak that has
not been found.

`0x0`-`0x400` remains the most attractive target on this SoC because it is fixed
by silicon rather than by firmware, so a write there would be model-independent.
The missing piece is a NULL-or-zero-pointer write, not the vectors themselves.

### Porting to an unknown build

This exploit is an instrument for one build, not a portable tool. Four constants
are build-specific:

1. **Video buffer base** (`0x02368200`). Every payload byte lands at
   `base + file_offset`, so this sets all placement. It moves with heap layout and
   feature set.
2. **Stack band** (`0x027FFxxx`). Where the overwritten return addresses live.
3. **Live `f_read` locals to preserve** (`FATFS*` at `0x27FFEEC`/`0x27FFEF0`,
   plus `0x27FFF20`/`0x27FFF24`/`0x27FFF28`). Without these the read destroys
   itself, as `PC=0xc2402888`.
4. **Read length** that reaches (2) from (1).

(1) is the hard one: the copy is a linear `base + offset` mapping, so *no* amount
of payload cleverness removes the need to know it — a jump sled or a relative
branch still has to be placed at a file offset that depends on `base`. Without the
target's firmware the only options are a blind search over `base` (with a visible
success signal and battery-pull recovery) or obtaining the image first.

### Measured: how far the heap targets move between builds

Same benign probe on all three dumps (oversized `idx1` read whose first delivered
byte is a self-validating 8-byte marker, so the address it lands on *is* the
destination base):

| build | AVI cache | video buffer | gap |
|---|---|---|---|
| original (KC02) | `0x0235A200` | `0x02368200` | `0xE000` |
| x8d | `0x021445C0` | `0x021525C0` | `0xE000` |
| z12 | `0x0215F5C0` | `0x0216D640` | `0xE080` |

Two useful facts fall out:

* The cache -> video gap is **stable** (`0xE000`, and `0xE080` on the dual-sensor
  z12). The relative layout is the same code; only the heap base moves. So the
  unknown collapses to **one 32-bit value**, not a general 2-D address space.
* The spread of that value across three builds is `0x215C40` = **2,186,304 bytes**
  (x8d and z12 sit within 110 KB of each other; the original is ~2.1 MB higher,
  i.e. it is not simply tracking app size).

Blind-search size at the video buffer base:

| granularity | candidates |
|---|---|
| 4 B (exact) | 546,577 |
| 4 KB | 534 |
| 64 KB | 34 |
| 256 KB | 9 |
| 1 MB | 3 |

So the answer to "is this 3 attempts or 3000" is: **it depends entirely on how
much address error the payload tolerates**, not on search cleverness. Exact
addressing is hopeless (546k boots). If the payload can absorb +/-32 KB it is 34
attempts; +/-512 KB makes it 5.

That is why a tolerance mechanism matters more than the exploit itself. The
promising shape is a **backward jump sled**: fill the payload with `l.j -8` hops
and park the real code at offset 0, so a hijacked return entering anywhere in the
already-written region walks *back* to the code. Tolerance then equals the payload
length, i.e. one guess covers every build measured above. Two complications are
not yet solved:

1. The stack slots must contain the hijack pointer, not sled instructions, and the
   stack band's offset from the base is itself build-specific (`stack_top` takes
   at least `0x02200000`/`0x02400000`/`0x02800000`).
2. Interleaving `[l.j -8][V]` pairs makes the backward walk skip the `V` words,
   which looks workable, but entry parity then decides whether a hijacked slot
   lands on a sled hop or on a `V` word.

Both are testable in the emulator by deliberately setting `V` far from the real
base.

### Tolerance attempt: failed, and the model behind it was wrong

Tried the backward jump sled, built to be build-independent: code at body offset
0, a landing pad at offset `P` whose `l.j` encodes a **displacement** (`-P`) rather
than an absolute address, then `l.j -8` hops, and the hijack pointer sprayed over
the rest. The hardware has **no delay slots**, so a hop takes effect immediately
and a 4-byte single-word sled would suffice; the 8-byte `[l.j -8][nop]` pair was
used first only because it is correct under either semantics.

Result: deterministic failure, twelve runs out of twelve.

```
PC=0x03FFFFFE      <- the sled word itself, used as a return address
```

Tested sled windows of 256 B, 1 KB, 4 KB and 32 KB, each with the pointer placed
inside the window: identical `PC=0x03FFFFFE` every time. So the hijacked slot is
**not** where the model put it, and shrinking the window does not help.

Then a payload filled with a unique per-slot pattern (`0x80000000 | offset`), to
decode the slot address straight from `PC`:

```
PC=0x00000258      <- the exception vector, i.e. the machine trapped
```

Two things follow, and the second is a correction to this document:

* The tolerant-sled model is wrong. The read is delivered to **two** destinations
  (AVI cache and video buffer, see above), and the hijackable frames evidently do
  not sit at a fixed offset from the video buffer, so "pointer lands in the sled"
  is not established by placing a sled at a buffer-relative offset.
* **Earlier "no hijack" verdicts from pattern runs were misclassified.** The
  harness treated `PC` in `0x170..0x400` as normal because that is where the boot/
  exception stubs live, so a run that trapped into the vector looked identical to
  a run that simply kept playing. Pattern-based runs that reported "no hijack"
  were in fact trapping. Any future pattern experiment must distinguish an
  exception entry from normal execution.

Diagnostics available (checked, useful for the next attempt):

* `qemu ... -d int -D <file>` works — the or1k target logs
  `INT: <type> pc=… epcr=… eear=… sr=…` on every exception, which is exactly the
  fault address that payload guessing cannot recover.
* Tick rate is high: 1446 `TICK (timer interrupt)` entries in ~6 s (~240/s). That
  matches the earlier `SR[TEE]` finding and is why the stub must mask `TEE`.
* A pattern-fill run produced **no** exception at all (`-d int` shows only TICKs),
  i.e. the pattern payload usually does not hijack — so it cannot be used to
  locate the slot. The earlier `PC=0x00000258` sample was not an exception entry.

### Reliability fixed, and what the hijack actually depends on

The ~50% success rate was a **payload-offset bug, not a race.** With the natural
`idx1.dwChunkOffset` the read starts at the chunk payload, so our body is shifted
by `appended` (`0x746`) and `body[k]` lands at `B + k - 0x746` — the sprayed
values missed the slots they were aimed at about half the time. Pointing
`dwChunkOffset` at the body start (`read_start = body[0]`, so `body[k] -> B + k`
exactly) makes it **5/5, deterministic**.

That also makes sentinel bisecting trustworthy. Filling a window with a *mapped*
sentinel (`0x03FFF000`, zero-filled, so a jump there self-loops visibly) and
looking for `PC=0x03FFF000`:

| window | result | meaning |
|---|---|---|
| `0x02368308..0x02368500` | 5/5 SUCCESS | not involved |
| `0x02368308..0x02368800` | 5/5 SUCCESS | not involved |
| `0x027E0000..0x027FEC00` | 5/5 SUCCESS | not involved |
| `0x027FEC00..0x027FFF34` | **0/5, no hijack** | required |

So the hijack depends on the **top stack band alone** containing the trampoline
address, and with a sentinel there the read does not hijack at all — it is not a
redirected jump, the path simply completes (PC stays in the app). Regions below
the band are irrelevant.

Consequence for porting is unchanged but now precise: the band must hold
`B + 0x2000` *exactly*, and the band's own address is `stack_top`-relative. Both
unknowns stay in play; what is fixed is that experiments are now 100% repeatable
in the emulator, so tolerance work can be measured instead of inferred.

Note: an earlier unreliability-era claim that "the hijack slot sits just above the
buffer" was wrong — it came from the same offset bug.

Unblocking this needs the fault address, not more payload guessing: read `EPC`/
`EEAR` at the trap (via the QEMU gdbstub or by locating the handler's save area),
which gives the hijacked slot address exactly. Only then can the sled geometry be
designed instead of assumed.

Cheap preconditions that can be tested blind, with no risk: whether the target
admits and plays the container at all, and whether it shares the `MOV`/`MIC`/`LOK`
naming gate. See `tools/exploit/avi-hijack/probe/`.

### Exfiltration on hardware: SD raw writes (chosen channel)

Constraints from the bench unit: the LCD cannot be used as an output channel, and
attaching USB switches the device out of the UI/playback mode the exploit needs.
So the card is the only channel. That is fine — it is also the cheapest:

* **Raw LBA write, not FatFs.** At the moment of hijack the FatFs state is
  mid-`f_read`; calling `f_open`/`f_write` on top of that risks corrupting the
  filesystem. Writing flash bytes to a fixed LBA range **beyond the partition**
  (e.g. the last 8 MB of the 31 GB card) needs no FS at all and cannot damage the
  user's files. Recovery is `dd` on a PC.
* **Cost is one constant, not a new blocker.** The payload needs the SD write
  entry point (and, for the FS route, `f_open`/`f_write`), which are the same class
  of build-specific address as the video buffer base — obtained from the same
  image. So exfiltration does not add a new porting problem.
* **Prototype now.** `original.bin` plus the emulator's writable SD image is enough
  to build and verify the dump payload end-to-end, leaving only re-addressing per
  build.
* **Size is not a constraint:** 4 MB of flash against 31 GB of card.

A cheaper *intermediate* channel exists on this hardware family: these are instant
print cameras, so a small leak (a few dozen bytes of addresses) could be printed
rather than dumped — enough to unlock the exploit on a build where only the
addresses are missing.

### Printer exfiltration: protocol recovered and working in the emulator

The thermal head is driven entirely through **SFR-mapped GPIO at fixed SoC
addresses**, so a print payload needs no per-build function pointers — the only
portable exfil channel available (LCD and USB are both ruled out).

Protocol, recovered from the emulator's head model and confirmed live:

| SFR | bits | meaning |
|---|---|---|
| `0x8004` | `0x40` rising/falling | job start / finish (finish emits the page) |
| `0x800c` | `0x8000` / `0x2000` rising | data bit / shift clock (384 dots per row) |
| `0x8004` | `0x20` rising | latch |
| `0x8000` | `0x200` rising | burn one row of dots |
| `0x8000`/`0x800c`/`0x8010` | `0x8`/`0x1`/`0xc00` | platen phase vector |

Three traps, all found by measurement:

1. **The job-start bit may already be set.** The firmware leaves `0x8004` with
   `0x40` high, so writing it again produces no edge and no page is emitted.
   Clear it, then set it.
2. **A clean motor step is one SFR write.** Each consecutive entry of the phase
   table `{10,8,9,1,5,4,6,2}` differs in exactly one bit of one SFR. Writing all
   three SFRs per change creates intermediate vectors that match *other* phases,
   which the model counts as `bad_steps` and the paper stops advancing.
3. **Never let the clock shadow carry bit 13.** `0x800c` read back with the clock
   line high, so every later motor write of that shadow raised the clock: one
   extra clock per row, which shears every glyph diagonally. Force it low.

Status in the emulator: job start/finish, 384-bit rows, latch, burn and paper
advance all work — `clocks=5376` (exactly 384 x 14), `steps=76`, `bad_steps=0`,
`384x20` page, two text lines emitted. Glyph placement still has a small
vertical/rotation misalignment, so the rendered text is not yet cleanly
decodable; that is a rendering bug in the payload, not in the channel.

**Toolchain constraint (important for any C payload):** the CPU has **no delay
slots**, but the generic `or1k-elf-gcc` fills delay slots with real instructions.
At `-O2` it put `l.addi r5,r5,1` in a branch delay slot, so the loop counter never
incremented and the payload hung. Compile payloads with **`-O0`**, where every
delay slot is `l.nop`. Hand-written assembly was always safe because it uses
`l.nop` explicitly — which is why only compiled C broke.

### Hardware portability (not yet proven)

Faithful on real hardware so far: the same stack addresses (linker script,
cross-checked against the sibling dumps), executable SDRAM with no NX, and the
OR1K `r9` return convention. Open before claiming hardware PC control:

1. SD `disk_read` PIO vs DMA and D-cache coherence across the copied region.
2. Interrupts landing mid-read on a clobbered frame — the stub is register-only
   and deliberately does not touch `r1`, so it survives an arbitrary entry state.
3. Live-build differences: none of these addresses are confirmed for the
   `349c:3301` unit on the bench.

Failure mode is a hang, not a brick: the payload is RAM-only and never writes
flash, so pulling the battery recovers.

### Reproduction

```
tools/exploit/avi-hijack/mkavi.py   AVI builder (must be stored as VIDEO/MOV00001.AVI)
tools/exploit/avi-hijack/entry.s   12-byte trampoline at 0x0236a200
tools/exploit/avi-hijack/real7.s   full-image payload at 0x02370000 (SR mask, 812 KB copy)
tools/exploit/avi-hijack/dump5.py  run + verify the full-image readout
tools/exploit/avi-hijack/cross.py  stack-band crossing instrumentation
```

## Retracted hypothesis — JPEG DHT value-table copy

`mjpeg_parse_image_header` (`0x02041864`) copies `sum(counts)` symbol bytes from a
DHT (0xFFC4) segment into a value table inside a `hal_sysMemMalloc(0x400)` block,
with no 256-symbol check visible in the decompiler. It **looks** like an
unbounded heap overflow of up to ~2 KB with attacker-chosen bytes.

It is not. Measured with calibrated DHT counts:

| table | requested | bytes written | first..last |
|---|---|---|---|
| 0 (`p0+0x1c0`) | 100 | 100 | `0x1c0..0x223` |
| 0 | 500 | 500 | `0x1c0..0x3b3` |
| 0 | 600 | **576** | `0x1c0..0x3ff` |
| 0 | 2032 | **576** | `0x1c0..0x3ff` |
| 1 (`p0+0x1cc`) | 640 | **564** | `0x1cc..0x3ff` |

The copy is clamped to the end of the 0x400-byte block. The bytes past the block
keep their pre-existing (boot-time) contents, which proves nothing was written
there. **The decompiled bound was misleading; there is a real limit.** Do not
re-derive this without measuring.

Two adjacent things that also look worse than they are:

* the SOF "component-ID" index is really a *sampling-factor* validation
  (first component 0x22, others 0x11 — i.e. 4:2:0 only) indexed by loop counter;
* oversized SOF dimensions do not widen the write: `ax32xx_mjpegDecodeSetSize`
  programs `dst_w<<16|dst_h` into the register the decode engine uses, so output
  extent follows the caller's destination, not the file's dimensions. Width is
  checked (`> 2000` rejected); height is not, but only affects scaling and the
  input pointer.

## Negative results (do not redo)

* **FatFs R0.13a is the patched revision.** `FF_USE_LFN=1`, `FF_FS_EXFAT=1`,
  `FF_CODE_PAGE=932`. Audited against the official `ff13a.zip`:
  `pick_lfn()` bounds the LFN offset (`i >= FF_MAX_LFN`), `load_xdir()` caps the
  exFAT entry block at 19×32 and verifies the set sum. Matches `libfs.a`'s
  `LfnBuf`=512 / `DirBuf`=608. No filesystem bug found.
* **MP3 (Helix-lineage fixed-point Layer III) holds.** 272 targeted side-info
  bit-flips (all fields that drive Huffman/scalefactor indexing: `part2_3_length`,
  `big_values`, `table_select`, `region*_count`, `scalefactor_compress`,
  `global_gain`, block type) produced no crash, no heap-guard hit and no
  out-of-family app state. The `audio_dispatch[i*3]` index that looked dangerous
  is a deliberate encoding that stays 0..8 for MPEG1/2/2.5.
* **JPEG viewer holds.** 132 structural mutations (SOF dimensions/0xFFFF,
  every segment length incl. 0 and 0xFFFF, DHT counts 0/1/0x7F/0x80/0xFF, DQT
  payloads, truncations, marker soup) produced no crash, no mode change, no
  heap-guard hit.
* **The `.LST` file-list cache is not a useful primitive.** A card-supplied
  `VIDEO.LST` with all 2048 entries marked used sets the add index to 0x800 and
  the next add writes 8 bytes at `0x020cca7c`. That address is inside the
  manager's own region for that experiment (the manager keeps five 0x4040-byte
  regions starting at `0x020c8a3c`), so it is an internal off-by-one, not a
  corruption primitive.

## Not yet tested

* **WAV / IMA-ADPCM — dead for these builds.** The SDK source
  (`multimedia/wav/wav.c`) has a real length bug: `wavDecodeOneFrame` takes
  `nBlockAlign` from the file and `imaadpcm_decode` expands it into the caller's
  buffer (`AUDIO_BUFF_SIZE` = 8192), i.e. up to `(cnt-4)*4+2` ≈ 256 KB. But the
  decoder object is **not linked into any of the three images**: the distinctive
  `adpcmStepTable`/`adpcmIndexTable` constants are absent from all three dumps
  (searched both 16- and 32-bit encodings) and no `adpcm` string exists. Only the
  RIFF/WAVE header templates are present (`WAVEfmt ` at file `0x8612C` /
  `0x6EF04` / `0x81670`), i.e. the encoder side. Do not pursue this unless a
  firmware is found that actually contains the tables.
* **AVI / `libavi` — solved; see Finding 3.** The frame service reads
  `idx1.dwChunkLength + 8` bytes with no clamp on the video path, and the
  overflow reaches the stack 4.8 MB above the buffer. The earlier conclusion
  that this was heap-only and therefore data-only was wrong: it came from
  stopping the overflow at +1.5 MB, where the only structures above the buffer
  really are inert data buffers. Pushed to the stack, the same read gives PC
  control at ~44% of runs. The audio branch is still unreached in the emulator
  (`aviDecodeOneFrame(...,2)` never succeeds) and is no longer needed.
* **USB device stack on hardware.** The SDK's `hal/dusb` parses host-supplied
  CBW/SCSI and EP0 setup bytes and is shared. The emulator has no USB device
  model, so this must be done on hardware or after adding a model.
* **Z-12 update path.** Different trigger/validation from `DestBin.bin`.

## Reproduction sketch

```sh
# card with a single photo
python3 mkcard.py /tmp/card.img '{"PHOTO/PHO00001.JPG": "file.jpg"}' 16
# boot, open the viewer (menu item 3) and snapshot
python3 probe.py --card /tmp/card.img --out /tmp/run
```

Deterministic baseline and per-case diffing live in `fuzz.py`; use
`menu_index=2` for the audio player (mode 5) and `3` for the viewer (mode 4).
