# Struct recovery from the SDK (Track 3a)

## Why DWARF, and how to get it

The vendor objects and our own SDK objects carry **no debug info** (checked: only `.symtab`, zero
`.debug_*` sections), so struct layouts cannot be recovered from the shipped binaries. The SDK *sources*
are available, so the layouts are obtained by compiling a synthetic translation unit with `-g`:

```bash
# 1. mirror the SDK headers so the Windows-style relative includes work (never write into the SDK tree):
mkdir -p /tmp/kc02-board-re/sdktypes/root && cd /tmp/kc02-board-re/sdktypes/root
for d in ax32_platform_demo ax32xx bwlib device hal multimedia sUI xos; do cp -a $SDK/$d $d; done
ln -sfn RES.H ax32_platform_demo/resource/res.h   # case aliases: the SDK was built on Windows
ln -sfn Maxlib bwlib/maxlib
# 2. one TU per header that defines a struct/union, with the SDK base typedefs as a prelude
# 3. compile every TU and one mega-TU that includes them all:
$PREFIX/bin/or1k-elf-gcc -DOR1K_NEWCPU -mno-delay -mhard-div -O0 -g \
    -fno-eliminate-unused-debug-types -w \
    -I. -Iax32_platform_demo -Ibwlib -Iax32_platform_demo/resource -c megafinal.c -o megafinal.o
# -> all 34 headers compile; 186 DW_TAG_structure_type, 7 unions, 1555 members, 413 typedefs
```

`-fno-eliminate-unused-debug-types` is essential: without it GCC drops every struct the TU never uses.

## What the dictionary contains

Type definitions with **exact offsets and sizes** (`docs/sdk_types.tsv`, dumped from Ghidra's DTM of
`megafinal.o` with `Kc02DumpTypes.java`): 181 structs, 6 unions, 1529 members, 57 typedefs. Highlights:

* **ISP / CMOS register blocks**: `AE` (180 B), `AWB` (376 B), `BLC` (28), `CCF` (36), `CCM` (24),
  `CH` (256), `DDC` (192), `EE` (50), `EXP` (64) — these are the ISP tuning structures the firmware
  writes; they explain the large parameter blocks in the imaging pipeline.
* **FatFs**: `FIL` (580 B), `DIR` (76 B) — the SD/FatFs file objects, usable directly.
* **AVI/ODML container structs** (`AVI_INDX_S`, `AVI_IX_HEADER_S`, …), **HAL_RTC_S**, **Stream_Head_S**,
  **R_ICON_S/R_STRING_S/RES_NAME_S** (Maxlib resources), **LAYOUT_VIEW_S**, **GSC_S**, **LOG_S**.

## Applied to the Ghidra project

`Kc02CreateSdkStructs.java` created **184 structs / 1291 members** in the main project's DTM at their exact
offsets (pass 1 creates every type with its length so cross-references resolve by name; pass 2 inserts each
member at its recorded offset). 540 members fell back to `byte[]` because their referenced type was
renamed during import (see below).

## DONE: clean dictionary + real prototypes applied (2026-10)

A dependency-free DWARF parser (`/tmp/kc02-board-re/dwarf_parse.py`, also in `knowledge/`) now parses
`objdump --dwarf=info` directly, resolving `DW_AT_type` through a DIE-offset index and recovering the names of
typedef'd anonymous structs (which Ghidra's importer had lost):

* **183 structs + 7 unions, 1520 members, 0 unresolved members, 0 name conflicts.**
  `uiWin` = `{msgDealInfor *msgDeal; widgetCreateInfor *widgetInfor; void *handle; byte repeateOpenSupport;}`
  (16 B) — the app window struct the whole UI layer is built on; `msgDealInfor` = `{uint msgId; void *func;}`
  (8 B). Nested type names now resolve (`FFOBJID`, `HGRM`, …) instead of `fsi_flag.conflictN`.
* Cross-checked against the Ghidra-derived layouts: offsets/sizes agree for `AE`, `AWB`, `DIR`, `FIL`, `CCF`,
  `CCM`; only type *names* differed (that was the mangling). Report: `knowledge/report_parser.md`.
* **Applied to the project:** 56 mangled types dropped, **190 structs / 1505 members created** at exact
  offsets (member fallbacks down from 540 to 12).
* **990 SDK prototypes applied** (return type + every parameter, `...` honoured as varargs), with the raw
  prototype and its header kept as the function's plate comment. 449 parameter slots are pointer types.
  Every prototype name matched the project function name at that address (990/990, 0 mismatches).
  Report + table: `knowledge/report_protos.md`, `docs/sdk_protos.tsv`.

Result in the decompiler — the UI/message layer is readable now::

    void msgFuncRegister(msgDealInfor *p0)
    {   ... fill 0xb8-byte handler table at DAT_ram_020ccc8c ...
        while (p0->msgId < 0x2e) { if (p0->func == 0) return; (&DAT_ram_020ccc8c)[p0->msgId] = p0->func; p0++; } }

    bool windowIsOpen(uiWin *p0) { return p0 ? ((int)p0->handle >> 0x18) & 1 : false; }

460 parameter/return types are still `undefined` (types from headers that did not compile into the synthetic
TU, plus SDK typedefs with no DWARF definition); the raw prototype comment still documents them.

## Earlier limitation (now resolved)

Ghidra's DWARF importer mangled part of the namespace: DIE name conflicts were rewritten as
`fsi_flag.conflictN`, and app-level types (`uiWin`, `msgDealInfor`, `widgetCreateInfor`, 38 `VIEW_*`,
`AVI_INDX*`) are present in the raw DWARF but not correctly named in the imported DTM. The fix is a small
**independent DIE parser** over `objdump --dwarf=info` output (index DIE offsets, resolve `DW_AT_type`
references to names, read `DW_AT_data_member_location`), which yields clean names + layouts; this also
recovers the anonymous-typedef ↔ struct pairing that Ghidra collapsed.

Then the payoff step: **apply SDK prototypes to the blob's functions**. 941 blob-named functions have a
prototype in the SDK headers, **267 of them take a pointer parameter** (`msgFuncRegister(msgDealInfor*)`,
`windowIsOpen(uiWin*)`, `frameWinCreateIndirect(widgetCreateInfor*, …)`, `hal_mjpegDecodeInputSet(...)`).
Setting those parameter types makes the decompiler propagate struct field names through every caller.
