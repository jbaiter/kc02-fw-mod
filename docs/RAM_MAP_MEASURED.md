# KC02 RAM map — measured, and candidate scratch regions

This answers one question with device evidence: **where can a larger validation
payload live?** Two previous assumptions turned out to be wrong, so read the
measured map before proposing a region.

Method: a read-only survey (`tools/survey_ram.py`) that only ever issues the stock
memory-transfer command with direction `device -> host` and `CALLBACK_SKIP`. It
never writes RAM and never executes RAM code. It verifies the pinned firmware
signatures first and stops at the first transport error. Findings are also
cross-checked against the app image, the vendor SDK linker script, and the RAM
globals inventory in `/tmp/kc02-board-re/ram_globals.json`.

## Measured map

| region | range | size | evidence |
|---|---|---|---|
| application code + rodata + data | `0x02000000`–`0x020C6200` | ~811 KiB | `FIRMWARE_MAP.md` |
| static RAM (`.bss` globals) | `0x020C6200`–`0x02103980` | ~295 KiB | start from `FIRMWARE_MAP.md`; **end measured below** |
| HAL dynamic pool | `0x02103980`–`0x027FEC00` | 7 320 192 B (~6.98 MiB) | **measured from the firmware** |
| initial stack | `0x027FEC00`–`0x02800000` | 5 120 B (`0x1400`) | vendor linker script; live frames observed |

The pool bounds are not a guess. The stock `ax32xx_sysInit` at CPU `0x0202B86C`
computes them in-place:

```
202b86c: l.movhi r3,0x210
202b870: l.ori   r3,r3,0x3980   ; r3 = 0x02103980  ->  *saddr
202b874: l.sw    0(r14),r3
202b880: l.movhi r3,0x6f
202b884: l.movhi r4,0x210
202b888: l.ori   r3,r3,0xb280   ; r3 = 0x006FB280  (__sdram_remain_size)
202b88c: l.ori   r4,r4,0x3980   ; r4 = 0x02103980
202b890: l.add   r3,r3,r4       ;    = 0x027FEC00  ->  *eaddr
202b894: l.sw    0(r2),r3
```

That matches the SDK source (`hal_sysInit` stores `&_sdram_remian_addr` and
`&_sdram_remian_addr + &__sdram_remain_size`, and `hal_sysMemMalloc` /
`hal_sysMemFree` allocate from it). **Everything from `0x02103980` to
`0x027FEC00` belongs to the firmware's allocator**, so it is not scratch space,
even though large parts of it read as zeros right now. An allocation may land on
any of it at any time.

Two supporting facts:

* SDRAM is 8 MiB. `ax329x.ld` declares `sdram : ORIGIN = 0x02000000,
  LENGTH = 0x00800000` and places `__stack` at `ORIGIN+LENGTH-4 = 0x027FFFFC`.
  The survey read `0x027FC000`–`0x02800000` successfully and found live frames
  (return addresses and `0x027FEAxx` pointers) — the stack.
* This firmware does **not** use libc `malloc`. The vendor's own linked build
  pulls only `memcpy`, `memset`, `strcat`, `strcmp`, `strcpy`, `strlen` and
  `strncmp` from `libc.a`; `sbrk` appears in no symbol table, and none of the
  `bwlib` archives define `malloc`/`_sbrk`/`free`. The heap is the HAL pool
  above, not a growing `brk` arena.

Bonus confirmation for the runtime port: the same `ax32xx_sysInit` disables both
interrupt levels with `l.mfspr r3,r4,0x0` / `l.mtspr r0,r3,0x18` where `r4 = 24`,
i.e. `SPR_SR = 0x18`. That is stock firmware code independently confirming the
status-register address used by the ported `setjmp`/`longjmp`.

## Runtime survey result

The read-only dump covers `0x020D7000`–`0x02104000`. Zero-byte runs of at least
1 KiB, plus whether any statically referenced global sits inside:

| span | size | all-zero | referenced globals inside | neighbours |
|---|---|---|---|---|
| `0x020D8900`–`0x020D9EFF` | 5 632 B | yes | none | our staging below, MJPEG globals above |
| `0x020DB200`–`0x020E30FF` | 32 512 B | yes | none | AVI record ctx B below, ctx A above |
| `0x020E4000`–`0x020E4AFF` | 2 816 B | yes | none | ISP/sensor block either side |
| `0x020E4C00`–`0x020E55FF` | 2 560 B | yes | none | — |
| `0x020E5700`–`0x020F2EFF` | 55 296 B | yes | sparsely referenced | printer/UI data either side |
| `0x020FC800`–`0x021036FF` | 28 160 B | yes | none | BSS tail data above |

Stability: after a full test batch that wrote a 2 KiB payload and executed it
(`--only echo-O0`), `0x020D7000`–`0x020DB000` diffed **0 bytes** against the
previous dump, the adjacent 5 632-byte run stayed zero, and the 32 512-byte run
was unchanged. So these spans are not written during normal idle operation.

The 55 296-byte span is a trap: the "no referenced global" heuristic and the
"reads as zero" heuristic disagree there, because it contains globals that happen
to be zero right now. The survey-based row above reflects the survey; treat the
55 KiB span as occupied until its sparse globals are accounted for. More
generally, neither heuristic alone is sufficient: `ram_globals.json` misses real
data (its highest referenced address is `0x020F2F1C`, yet data is live at
`0x02103700`–`0x02103980`), and a zero run can hide a reserved buffer.

## Recommendation

**Extend the existing staging slot rather than relocating.** The current slot is
`SHIM_ADDR 0x020D8000` / `CANARY_ADDR 0x020D8080` / `BUFFER_ADDR 0x020D8100`
with `STAGING_LIMIT 0x020D9000`. Raising the limit to `0x020D9F54` gives a
**7 764-byte** payload buffer (`0x020D8100`–`0x020D9F53`):

* it is contiguous with the region already exercised by hundreds of
  write/readback/execute cycles, so no new memory discipline is introduced;
* it is bounded below by referenced scalars at `0x020D7F50` and above by
  referenced MJPEG globals at `0x020D9F54`/`0x020D9F5C`/`0x020D9F9C`/`0x020D9FA0`
  and a live value at `0x020D9FF8`;
* it was zero before use and unchanged after a test batch;
* it is enough for every currently blocked profile: the largest overshoot is
  `double_div-O0` at 2 412 bytes over the 1 792-byte budget, i.e. ~4.2 KiB of
  code, which fits with a packet area to spare.

`STAGING_LIMIT` is also not a real reservation. `tools/patch_child_mode.py`
rewrites flash file offsets in the app image, and its only RAM state bytes are at
`0x020D41D4/D5` — far below the slot. The comment "documented patch regions at
0x020D9000" is conservative bookkeeping, not a live constraint.

If a much larger region is wanted later, the two clean candidates are
`0x020DB188`–`0x020E3180` (32 760 B) and `0x020FC800`–`0x021036FF` (28 160 B),
both all-zero, stable and free of referenced globals. Neither is recommended as
a default yet: the first sits exactly between the two AVI record contexts, so it
is plausibly the AVI capture buffer and may be rewritten while recording video;
the second is in the BSS tail with uncharacterised neighbours.

## Adopted layout (implemented)

The adjacent extension was adopted. Code and the execution packet are now separate
regions, so a large code image does not force a large USB transfer:

| Purpose | Address | Size | Notes |
| --- | --- | --- | --- |
| Dispatcher / SPI adapter | `0x020D8000` | <= 128 B | unchanged |
| Assembly canary | `0x020D8080` | 4 B | unchanged |
| Code image | `0x020D8100` | <= 5632 B | linked here, uploaded in 2048-byte chunks |
| Execution packet | `0x020D9700` | 2048 B | passed in `r3`, returned by the helper |
| slack | `0x020D9F00` | 84 B | below the live global at `0x020D9F54` |

The packet keeps its original internal offsets (inputs at +1792, header at +1856,
results at +1888, guard at +2032) because payloads derive those from the pointer
they receive, so no payload source changed for the move. `DISPATCH` is unchanged:
the dispatcher still jumps to `0x020D8100`.

Code images are padded to 16 bytes. That is not cosmetic: newlib's `strcmp` reads
a full word and can touch up to three bytes past the end of `.rodata`, which is
fine on the device but was outside the offline model's mapped bytes. Padding makes
device and model agree on exactly what is present.

Result: all **160** profiles build, none is over budget, and all **1200** vectors
pass on the device. The 33 profiles that the old 1792-byte budget excluded —
64-bit divide/remainder and the double-precision arithmetic closures, the largest
being `double_div-O0` at roughly 4.2 KiB of code — now fit, and `double_div-O0`
exercises a two-chunk code upload.

Only 16 bytes of padding per profile are added, so almost every profile still
uploads in a single proven 2048-byte transfer.

## Validation protocol for any newly adopted region

Nothing above proves ownership. Before a region becomes a default:

1. Confirm it is zero and unchanged by re-running the survey (read-only, no
   writes) twice with real work in between.
2. Upload a distinctive pattern across the whole span in 2 048-byte transfers,
   read every byte back, and require an exact match.
3. Execute a payload from it that re-reads the whole span and reports a hash, so
   the CPU's view and the host's view are both checked.
4. Re-survey after exercising the features that plausibly own the area — video
   record, playback, photo capture, print — and require the pattern to survive.
5. Only then widen the harness. Keep per-transfer length at 2 048 bytes; a larger
   *region* does not require larger USB transfers.

## Reproducing

```sh
# read-only: no RAM writes, no RAM execution
.env/bin/python tools/survey_ram.py --start 0x020D7000 --end 0x020DB000
# dumps land in patched/toolchain-tests/runs/<utc>-survey/{survey.bin,survey.json}
```

The tool refuses any payload, any callback other than `CALLBACK_SKIP`, and any
address outside its declared window (`0x020C6000`–`0x02800000`).
