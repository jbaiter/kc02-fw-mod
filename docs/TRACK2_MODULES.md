# Track 2 — evidence-based naming of the product-specific code

The remaining unnamed functions are **product code** (no vendor counterpart exists: best similarity 0.40
against all 1363 vendor entries), so they were named from behaviour: decompiled body + machine-extracted
evidence (SPR/SFR registers touched, referenced strings incl. GBK/CJK, named callees/callers, tables).

Worklist generator: `/tmp/kc02-board-re/name_evidence.py` -> `name_worklist.tsv`
(312 of 370 unnamed functions carry at least one machine-checkable signal:
188 call a named function, 128 are called by one, 27 reference strings, 16 touch SPR/SFR registers).

Bodies: `Kc02DumpUnnamed.java` -> `unnamed_decomp.txt` (370 functions, 389 KB).

## Method

Split into 11 address-contiguous batches of ~34 functions (`track2/batch_NN.txt`, each carrying the rules,
the evidence table and the decompiled bodies), named in parallel by `deepseek/deepseek-flash` worker
subagents (`runs.all`, concurrency 6, ~9.5 min wall). Each returned
`0xADDR<TAB>name<TAB>certain|likely|guess<TAB>rationale` plus a module hypothesis.

Every proposal was validated before application: address must be a currently-`FUN_` entry, name must match
`[a-z][a-z0-9_]{2,31}`, not a C keyword, no duplicate inside or across batches, no collision with an
existing project name, and the name's subsystem family must not contradict the extracted evidence.
**Result: 370/370 well-formed, 0 rejects, 0 duplicates, 0 collisions, 0 evidence contradictions.**

**Applied: 206 names** (`certain` 18 + `likely` 188) with a provenance comment citing the batch.
Project total: **2314 functions, 2150 named = 92.9%, 164 unnamed.**

Kept for review (`track2/review_names.tsv`): the **164 `guess`-tier** proposals, i.e. roles inferred from
body structure alone. A second opinion from a stronger model on those 164 (with the same evidence packets)
is the obvious next step before applying them.

## Subsystems identified in this pass

* **MP3 decoder** (batch 10, `0x0205EF80`-`0x02064B0C`): frame sync acquisition, frame-length computation
  (`(12*bitrate/samplerate + padding)*4`, or 144/72*bitrate/samplerate for layer 3), granule side info,
  MPEG-1/LSF scalefactors, Huffman decode, requantisation, alias reduction, 18->36 IMDCT + windowing.
  Verified by hand: the applied `audio_calc_frame_length` body contains exactly the constants and offsets
  its rationale claims.
* **Mini-games** (batches 5-6, `0x0201C1C0`-`0x0201FD74`): 8x8 tile-board game engine (playfield scan /
  level-clear test, player update, respawn delay) and a 20x14 falling-block/snake-style grid game,
  plus a shared clipped-text HUD.
* **Software I2C bit-bang + UI widget pool** (batch 8, `0x0202A1D0`-`0x02046150`): I2C bus ops, SFR/GPIO
  bring-up, MJPEG/LCD/OSD glue.
* **Photo effect filters** (batch 7): JPEG->YUV filters, `win_*`/`widget_*` toolkit.
* **JPEG/MJPEG codec core + FatFs + record tables** (batch 11, `0x02064FDC`-`0x0207B77C`).
* **AVI/MJPEG record & playback, USB device, ISP/LCD overlay** (batch 9).
* **Playback window handlers + SD free-space/record-timer OSD** (batch 3).
* **Board init/ioctl, `.LST` file-list manager, NVM config/record access** (batch 1).
* **Task/message dispatch, power and LCD-timeout ticks** (batch 2).
