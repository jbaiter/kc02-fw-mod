# SDK type-dictionary parser — self-check report

Deliverables:

* `/tmp/kc02-board-re/dwarf_parse.py` — dependency-free Python 3 parser.
* `/tmp/kc02-board-re/sdk_types_clean.tsv` — clean, de-mangled type dictionary
  (`S`/`M`/`U`/`N` rows, tab separated).
* this report.

Reproduce with:

```
python3 /tmp/kc02-board-re/dwarf_parse.py
```

The script has no arguments and is idempotent (byte-identical output on re-run).

## 1. Run summary

Input : `/tmp/kc02-board-re/sdktypes/dwarf-final.txt` (single CU, 19240 lines)
Output: `/tmp/kc02-board-re/sdk_types_clean.tsv` (1710 rows)

Raw DWARF inventory (DIEs at the top level of the CU):

| kind | raw DIEs | emitted |
|---|---|---|
| structs (`DW_TAG_structure_type`) | 186 | 183 |
| unions (`DW_TAG_union_type`)      | 7   | 7 |
| members (`DW_TAG_member`)         | 1555 | 1520 |
| typedefs (`DW_TAG_typedef`)       | 413 | — (folded into names) |

* **3 structs skipped as compiler internals**: `__tm`, `__sbuf`, `__FILE`
  (newlib, decl_file 9). They are `__lowercase` newlib internals.
* The SDK's own dunder globals `__RAM_USE__`, `__FIFO_USB__`, `__CSI_STAT__`
  match `__*` but are **SDK types** and are kept (see §4).
* **Name conflicts: 0.** No two emitted types share a name with different
  layouts, so the "keep the larger one" conflict rule never fired.
* **Unresolved members: 0.** Every one of the 1520 emitted members resolved to a
  concrete type and byte size. No `undefined` type strings were produced, and no
  `DW_AT_type` reference was dangling (all 4628 DIEs indexed up-front by
  offset).
* Unions emit `N` member rows with the same columns as `M` rows, per spec.

`DW_AT_type` is resolved by DIE offset: the parser builds `offset -> DIE` first,
then walks `typedef -> pointer/const/volatile -> base/struct/union/enum/array`
chains. Anonymous structs/unions and GCC's private tag convention (`_uiWin`)
are named from the referring `DW_TAG_typedef` (see §4).

## 2. The 20 named SDK types

All required names are present exactly once each.

| # | name | kind | size (bytes) | members |
|---|---|---|---|---|
| 1 | `uiWin` | struct | 16 | 4 |
| 2 | `msgDealInfor` | struct | 8 | 2 |
| 3 | `widgetCreateInfor` | struct | 40 | 15 |
| 4 | `AVI_INDX_S` | struct | 16 | 3 |
| 5 | `AVI_IX_HEADER_S` | struct | 32 | 9 |
| 6 | `HAL_RTC_S` | struct | 12 | 7 |
| 7 | `Stream_Head_S` | struct | 36 | 4 |
| 8 | `LAYOUT_VIEW_S` | struct | 12 | 5 |
| 9 | `R_ICON_S` | struct | 24 | 7 |
| 10 | `R_STRING_S` | struct | 16 | 5 |
| 11 | `RES_NAME_S` | struct | 8 | 2 |
| 12 | `FIL` | struct | 580 | 9 |
| 13 | `DIR` | struct | 76 | 8 |
| 14 | `AE` | struct | 180 | 2 |
| 15 | `AWB` | struct | 376 | 27 |
| 16 | `CCF` | struct | 36 | 9 |
| 17 | `CCM` | struct | 24 | 4 |
| 18 | `CH` | struct | 256 | 11 |
| 19 | `DDC` | struct | 192 | 14 |
| 20 | `EE` | struct | 50 | 8 |
| 21 | `EXP` | struct | 64 | 9 |

(`EXP` is the 21st because the task list contains 21 names; all are present.)

## 3. Cross-check vs Ghidra export (`/tmp/kc02-board-re/sdk_types.tsv`)

For each struct the two layouts are shown member by member.
`=` in the last column means the running parser and Ghidra agree on the member
offset **and** byte size.

### 3.1 `AE`

Ghidra shows the field names and one nested type name mangled (Ghidra's conflict
renamer), but every offset/size is identical.

| # | parser off/size/type/field | Ghidra off/size/type/field | |
|---|---|---|---|
| 0 | 0/64 `EXP` exp_adapt | 0/64 `EXP` fsi_flag |=|
| 1 | 64/116 `HGRM` hgrm_adapt | 64/116 `fsi_flag.conflict31` fsi_flag |=|

**Verdict: AGREES on layout** (size 180, 2 members, identical offsets/sizes).
Disagreements: (a) field names 0 and 1 are lost in Ghidra (`fsi_flag`);
(b) member 1's type name is `HGRM` in the parser but the mangled
`fsi_flag.conflict31` in Ghidra (same 116-byte layout). Member 0's type name
`EXP` matches.

### 3.2 `AWB`

| # | parser off/size/type/field | Ghidra off/size/type/field | |
|---|---|---|---|
| 0 | 0/4 `uint` seg_mode | 0/4 `u32` fsi_flag |=|
| 1 | 4/4 `uint` rg_start | 4/4 `u32` fsi_flag |=|
| 2 | 8/4 `uint` rgmin | 8/4 `u32` fsi_flag |=|
| 3 | 12/4 `uint` rgmax | 12/4 `u32` fsi_flag |=|
| 4 | 16/4 `uint` weight_in | 16/4 `u32` fsi_flag |=|
| 5 | 20/4 `uint` weight_mid | 20/4 `u32` fsi_flag |=|
| 6 | 24/4 `uint` ymin | 24/4 `u32` fsi_flag |=|
| 7 | 28/4 `uint` ymax | 28/4 `u32` fsi_flag |=|
| 8 | 32/4 `uint` hb_rate | 32/4 `u32` fsi_flag |=|
| 9 | 36/4 `uint` hb_class | 36/4 `u32` fsi_flag |=|
| 10 | 40/4 `uint` hr_rate | 40/4 `u32` fsi_flag |=|
| 11 | 44/4 `uint` hr_class | 44/4 `u32` fsi_flag |=|
| 12 | 48/4 `uint` awb_scene_mod | 48/4 `u32` fsi_flag |=|
| 13 | 52/20 `uint[5]` manu_awb_gain | 52/20 `u32[5]` fsi_flag |=|
| 14 | 72/4 `uint` yuv_mod_en | 72/4 `u32` fsi_flag |=|
| 15 | 76/32 `uint[8]` cb_th | 76/32 `u32[8]` fsi_flag |=|
| 16 | 108/32 `uint[8]` cr_th | 108/32 `u32[8]` fsi_flag |=|
| 17 | 140/32 `uint[8]` cbcr_th | 140/32 `u32[8]` fsi_flag |=|
| 18 | 172/4 `uint` ycbcr_th | 172/4 `u32` fsi_flag |=|
| 19 | 176/4 `uint` manu_rgain | 176/4 `u32` fsi_flag |=|
| 20 | 180/4 `uint` manu_ggain | 180/4 `u32` fsi_flag |=|
| 21 | 184/4 `uint` manu_bgain | 184/4 `u32` fsi_flag |=|
| 22 | 188/4 `uint` rgain | 188/4 `u32` fsi_flag |=|
| 23 | 192/4 `uint` ggain | 192/4 `u32` fsi_flag |=|
| 24 | 196/4 `uint` bgain | 196/4 `u32` fsi_flag |=|
| 25 | 200/48 `ushort[8][3]` seg_gain | 200/48 `u16[8][3]` fsi_flag |=|
| 26 | 248/128 `byte[128]` awb_tab | 248/128 `u8[128]` fsi_flag |=|

**Verdict: AGREES on layout** (size 376, 27 members, all 27 offsets/sizes and
array shapes identical, including the `[8][3]` and `[128]` arrays).
Disagreements: field names are lost in Ghidra; type spellings differ because
Ghidra kept the SDK typedef names (`u32`/`u16`/`u8`) while the parser normalises
to the canonical basics (`uint`/`ushort`/`byte`). They are the same widths.

### 3.3 `DIR`

| # | parser off/size/type/field | Ghidra off/size/type/field | |
|---|---|---|---|
| 0 | 0/40 `FFOBJID` obj | 0/40 `fsi_flag.conflict109` obj |=|
| 1 | 40/4 `uint` dptr | 40/4 `fsi_flag.conflict4` fsi_flag |=|
| 2 | 44/4 `uint` clust | 44/4 `fsi_flag.conflict4` fsi_flag |=|
| 3 | 48/4 `uint` sect | 48/4 `fsi_flag.conflict4` fsi_flag |=|
| 4 | 52/4 `PTR:byte` dir | 52/4 `PTR:fsi_flag.conflict` dir |=|
| 5 | 56/12 `byte[12]` fn | 56/12 `fsi_flag.conflict[12]` fn |=|
| 6 | 68/4 `uint` blk_ofs | 68/4 `fsi_flag.conflict4` fsi_flag |=|
| 7 | 72/4 `char*` pat | 72/4 `PTR:fsi_flag` pat |=|

**Verdict: AGREES on layout** (size 76, 8 members, identical offsets/sizes).
Disagreements: nested type names are mangled in Ghidra
(`FFOBJID` -> `fsi_flag.conflict109`, `PTR:byte` -> `PTR:fsi_flag.conflict`,
`char*` -> `PTR:fsi_flag`); fields `dptr`/`clust`/`sect`/`blk_ofs` are lost in
Ghidra. Fields `obj`, `dir`, `fn`, `pat` survive in both.

### 3.4 `FIL`

| # | parser off/size/type/field | Ghidra off/size/type/field | |
|---|---|---|---|
| 0 | 0/40 `FFOBJID` obj | 0/40 `fsi_flag.conflict109` obj |=|
| 1 | 40/1 `byte` flag | 40/1 `fsi_flag.conflict` fsi_flag |=|
| 2 | 41/1 `byte` err | 41/1 `fsi_flag.conflict` err |=|
| 3 | 44/8 `uint64` fptr | 44/8 `fsi_flag.conflict6` fsi_flag |=|
| 4 | 52/4 `uint` clust | 52/4 `fsi_flag.conflict4` fsi_flag |=|
| 5 | 56/4 `uint` sect | 56/4 `fsi_flag.conflict4` fsi_flag |=|
| 6 | 60/4 `uint` dir_sect | 60/4 `fsi_flag.conflict4` fsi_flag |=|
| 7 | 64/4 `PTR:byte` dir_ptr | 64/4 `PTR:fsi_flag.conflict` fsi_flag |=|
| 8 | 68/512 `byte[512]` buf | 68/512 `fsi_flag.conflict[512]` buf |=|

**Verdict: AGREES on layout** (size 580, 9 members, identical offsets/sizes;
note the 8-byte `FSIZE_t`/`QWORD` at 44, correctly 8 bytes in both). The parser
names it `uint64`; Ghidra shows the mangled conflict type, same width.
Disagreements: nested type names are mangled in Ghidra; fields
`flag`/`fptr`/`clust`/`sect`/`dir_sect`/`dir_ptr` are lost in Ghidra. Fields
`obj`, `err`, `buf` survive in both.

### 3.5 `CCF`

| # | parser off/size/type/field | Ghidra off/size/type/field | |
|---|---|---|---|
| 0 | 0/4 `uint` rate | 0/4 `u32` fsi_flag |=|
| 1 | 4/4 `uint` ymax | 4/4 `u32` fsi_flag |=|
| 2 | 8/4 `uint` th | 8/4 `u32` th |=|
| 3 | 12/4 `uint` wdc_en | 12/4 `u32` fsi_flag |=|
| 4 | 16/4 `uint` wclass | 16/4 `u32` fsi_flag |=|
| 5 | 20/4 `uint` wymin | 20/4 `u32` fsi_flag |=|
| 6 | 24/4 `uint` mean_en | 24/4 `u32` fsi_flag |=|
| 7 | 28/4 `uint` dn_class | 28/4 `u32` fsi_flag |=|
| 8 | 32/4 `uint` ccf_en | 32/4 `u32` fsi_flag |=|

**Verdict: AGREES on layout** (size 36, 9 members, identical offsets/sizes,
all `uint`/`u32`). Disagreements: field names lost in Ghidra except `th`;
`uint` vs `u32` spelling is a normalisation difference only.

### 3.6 `CCM`

| # | parser off/size/type/field | Ghidra off/size/type/field | |
|---|---|---|---|
| 0 | 0/18 `ushort[9]` ccm | 0/18 `u16[9]` ccm |=|
| 1 | 18/2 `short` s41 | 18/2 `fsi_flag.conflict1` s41 |=|
| 2 | 20/2 `short` s42 | 20/2 `fsi_flag.conflict1` s42 |=|
| 3 | 22/2 `short` s43 | 22/2 `fsi_flag.conflict1` s43 |=|

**Verdict: AGREES on layout** (size 24, 4 members, identical offsets/sizes and
the `[9]` array). Disagreements: `ushort` vs `u16` and `short` vs mangled
conflict type names; all field names (`ccm`,`s41`,`s42`,`s43`) match.

### 3.7 Cross-check summary

| struct | sizes | member count | offsets/sizes | verdict |
|---|---|---|---|---|
| `AE` | 180 = 180 | 2 = 2 | all match | layout AGREES |
| `AWB` | 376 = 376 | 27 = 27 | all match | layout AGREES |
| `DIR` | 76 = 76 | 8 = 8 | all match | layout AGREES |
| `FIL` | 580 = 580 | 9 = 9 | all match | layout AGREES |
| `CCF` | 36 = 36 | 9 = 9 | all match | layout AGREES |
| `CCM` | 24 = 24 | 4 = 4 | all match | layout AGREES |

**No layout disagreement was found.** The only disagreements are semantic
naming differences that are exactly the damage this parser exists to repair:
Ghidra's conflict renamer replaced nested type names with `fsi_flag.conflictN`
and erased most field names, whereas the parser recovers the true names from the
DWARF. Type-spelling differences (`uint`/`u32`, `ushort`/`u16`, `byte`/`u8`)
are normalisation only and are width-identical.

## 4. Name recovery for typedef'd / anonymous types

Two naming rules are applied, both driven by the DWARF:

1. **Anonymous struct/union with a typedef.** If a structure/union DIE has no
   `DW_AT_name`, its name is taken from the `DW_TAG_typedef` that points at it.
   Example: the 180-byte struct at `0x242f` has no name; typedef `AE` points at
   it, so it is emitted as `AE`. Same for `EXP`, `HGRM`, `AWB`, etc.
2. **GCC private tag.** GCC emits `_uiWin`, `_widgetCreateInfor`,
   `_msgDealInfor`, `_winObj`, ... for `typedef struct _X {...} X;`. The parser
   prefers the public typedef alias when the DWARF name begins with `_`, so
   `uiWin`, `msgDealInfor` and `widgetCreateInfor` appear by their real names.

Only 3 composites were dropped (see §1). The retained names that start with
`__` (`__RAM_USE__`, `__FIFO_USB__`, `__CSI_STAT__`) are SDK globals declared in
`hal/inc/hal_sys.h` and `ax32xx/inc/ax32xx_csi.h`, not compiler internals.

Honest limitation: this parser keeps newlib typedef-structs whose names do **not**
match the task's skip patterns (`_reent`, `_mbstate_t`, `_Bigint`, `div_t`,
`ldiv_t`, `lldiv_t`, `cookie_io_functions_t`). They are not SDK types; they were
kept because the spec only names `Elf32_*`, `__*` and `std::*` as internals and
because `_reent`/`_atexit`/`_rand48` merely start with a single underscore. If a
stricter filter is wanted, add them to the internal test. This affects at most
11 entries and none of the 20 target types.

## 5. Raw DIE snippet vs. parser output

One example, `HAL_RTC_S` (struct DIE at offset `0x3e31`, 12 bytes). Raw
`objdump --dwarf=info` text:

```
 <1><3e31>: Abbrev Number: 15 (DW_TAG_structure_type)
    <3e32>   DW_AT_name        : (indirect string, offset: 0x64f1): HAL_RTC_S
    <3e36>   DW_AT_byte_size   : 12
    <3e37>   DW_AT_decl_file   : 33
    <3e38>   DW_AT_decl_line   : 28
    <3e39>   DW_AT_sibling     : <0x3e92>
 <2><3e3d>: Abbrev Number: 13 (DW_TAG_member)
    <3e3e>   DW_AT_name        : (indirect string, offset: 0x636): year
    <3e44>   DW_AT_type        : <0x310>
    <3e48>   DW_AT_data_member_location: 0
 <2><3e49>: Abbrev Number: 13 (DW_TAG_member)
    <3e4a>   DW_AT_name        : (indirect string, offset: 0x3181): month
    <3e50>   DW_AT_type        : <0x290>
    <3e54>   DW_AT_data_member_location: 2
 <2><3e55>: Abbrev Number: 16 (DW_TAG_member)
    <3e56>   DW_AT_name        : day
    <3e5c>   DW_AT_type        : <0x290>
    <3e60>   DW_AT_data_member_location: 3
 <2><3e61>: Abbrev Number: 13 (DW_TAG_member)
    <3e62>   DW_AT_name        : (indirect string, offset: 0x45a7): hour
    <3e68>   DW_AT_type        : <0x290>
    <3e6c>   DW_AT_data_member_location: 4
 <2><3e6d>: Abbrev Number: 16 (DW_TAG_member)
    <3e6e>   DW_AT_name        : min
    <3e74>   DW_AT_type        : <0x290>
    <3e78>   DW_AT_data_member_location: 5
 <2><3e79>: Abbrev Number: 16 (DW_TAG_member)
    <3e7a>   DW_AT_name        : sec
    <3e80>   DW_AT_type        : <0x290>
    <3e84>   DW_AT_data_member_location: 6
 <2><3e85>: Abbrev Number: 13 (DW_TAG_member)
    <3e86>   DW_AT_name        : (indirect string, offset: 0x80db): totalsec
    <3e8c>   DW_AT_type        : <0x394>
    <3e90>   DW_AT_data_member_location: 8
 <2><3e91>: Abbrev Number: 0
```

Type references used above (resolved by offset):

* `<0x310>` -> `DW_TAG_typedef u16` -> `<0x5e>` short unsigned int, size 2 -> `ushort`
* `<0x290>` -> `DW_TAG_typedef u8`  -> `<0x3a>` unsigned char, size 1 -> `byte`
* `<0x394>` -> `DW_TAG_typedef u32` -> `<0x82>` long unsigned int, size 4 -> `uint`

The exact parser output for this struct (`grep -P '^(S|M)\tHAL_RTC_S\t' sdk_types_clean.tsv`):

```
S	HAL_RTC_S	12
M	HAL_RTC_S	0	ushort	2	year
M	HAL_RTC_S	2	byte	1	month
M	HAL_RTC_S	3	byte	1	day
M	HAL_RTC_S	4	byte	1	hour
M	HAL_RTC_S	5	byte	1	min
M	HAL_RTC_S	6	byte	1	sec
M	HAL_RTC_S	8	uint	4	totalsec
```

The layout matches `ax32xx/inc/.../hal_rtc.h` semantics: `year` is a 16-bit
value at offset 0, then five `u8` fields, then a 32-bit `totalsec` at offset 8
(offset 7..8 padding).

## 6. Method notes

* DIEs are indexed by offset first; `DW_AT_type` is always followed through the
  offset map. Chains handled: `typedef`, `pointer_type`, `const_type`,
  `volatile_type`, `array_type`(+`subrange_type`), `base_type`,
  `structure_type`, `union_type`, `enumeration_type`, `subroutine_type`.
* Arrays: element type is resolved recursively; dimensions come from the
  `DW_AT_upper_bound+1` of each `DW_TAG_subrange_type` (multi-dimensional
  supported, e.g. `ushort[8][3]`, `byte[24][8192]`).
* Pointers: `PTR:<TypeName>`; `char*` and `void*` are used for char/void/no
  target; function pointers (`pointer -> subroutine_type`) are rendered `void*`.
* No `DW_TAG_bitfield`/`DW_AT_bit_size` appears in this DWARF, so no
  `BITFIELD:` rows were produced. The branch is implemented regardless.
* Deduplication is by output name, keeping the DIE with the most members; a
  conflict is recorded and reported if two same-named types disagree. Zero
  conflicts occurred for this input.
