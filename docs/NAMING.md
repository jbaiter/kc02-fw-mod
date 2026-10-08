# Naming the remaining functions — method, results, ceiling

Two independent naming tracks have been run beyond the original vendor-listing alignment.
This file records exactly how each name was produced, so the confidence of every label is auditable.

## Track 1 — ordered similarity alignment against the vendor's linked build

**Inputs.** The leaked SDK ships *both* the sources and a **fully linked sibling build** of the same SDK:
`ax32_platform_demo/output/ax329x_sdk.lst` (2743 app symbols) and `ax329x_sdk.bin` (a flash image whose
`.text` maps `file = 0x2000 + (VMA − 0x02000000)` — verified byte-for-byte against the listing).

**Method** (`/tmp/kc02-board-re/seq_align.py`, also in `knowledge/`):

1. **Real function entries on the vendor side** — the listing's symbols include local labels, which truncate
   naive symbol extents (this silently broke call-set comparison in the first attempt). Entries are
   therefore taken as *the first symbol plus every symbol that is the target of some `l.jal`*.
2. **Monotonic anchors** — blob functions whose name is a *unique* vendor symbol name give
   (blob addr, vendor addr) pairs; a longest-increasing-subsequence pass removes mis-ordered ones
   (1026 name anchors → **995 monotonic**).
3. **Pair score** (0..1), over the same symmetric window on both sides:
   `0.50 · positional masked-word match + 0.25 · opcode-stream similarity (difflib) + 0.25 · named-callee Jaccard`.
   Registers are kept, immediates masked (`0xFFFF0000` for the 16-bit forms, `0xFC000000` for `l.j/l.jal`).
4. **Needleman–Wunsch DP** inside each anchor segment, allowing gaps on either side; score ≥ 0.72 applied.

**Calibration** (known anchors, same name on both sides): mean 0.814; identical code scores 1.000;
identical code with no calls on either side scores 0.750. This is why the apply threshold is 0.72.

**Result: +25 names** (`docs/seq_names.tsv`), all carrying an `ordered-alignment … score x.xx` comment;
13 further candidates sit in the 0.60–0.72 review band (`docs/seq_review.tsv`).

**Ceiling — and why.** The yield is small because the sibling build simply does not contain most of the
remaining code. Measured, not guessed: the 105-function block at `0x0201A690`–`0x0201F710` (the product's
own UI/widget layer, which *calls* `R_drawAsciiChar`/`drawChar`/`asset_decode` and references glyph tables)
scores at most **0.40** against *all 1363* vendor entries. Best-match analysis shows the same for the
printer/sensor block. Conclusion: the remaining ~370 functions are **product-specific code**, not SDK code,
so vendor names do not exist for them — they must be named from behaviour (Track 2) or from our own SDK
compilation.

**By-product: 92 suspect names.** Scoring every name-based anchor exposed **92 named blob functions whose
vendor counterpart does not match** (`docs/suspect_names.tsv`; worst: `strncmp` 0.13, `winCreate` 0.12,
`xsum32` 0.10, `hal_mjpegSizecalculate` 0.10). Each of those functions now carries a
`NAME-TO-VERIFY: …` comment in Ghidra instead of being silently trusted. Note some are legitimately
different (e.g. multiple static implementations of the same helper), so this is a review list, not an
error list.

## Track 2 — evidence-driven naming of product code (next)

`/tmp/kc02-board-re/name_evidence.py` harvests, for every unnamed function: SPR/SFR addresses touched,
literal data references resolved against a **5926-entry string map (379 ASCII + 5547 GBK/CJK)** and the
typed tables, named callees, named callers, and the enclosing module run.

Coverage measured over the then-395 unnamed functions: **337 (85%) carry at least one signal** (the 25 named by
Track 1 came out of this set) —
205 call a named function, 153 are called by one, 30 touch SPR/SFR registers, 28 reference strings,
8 reference known tables. This is the worklist for naming product code (decompiled body + these signals),
applied in batches with a provenance comment; names are ours (`kc02_*`) and describe behaviour.

## Rules

* No name is ever overwritten; the previous name stays in the comment.
* Every inferred name records **method + evidence + score** in its comment.
* Ambiguity is preserved, not hidden: `unk_<addr>_<hint>` stays until evidence arrives.
