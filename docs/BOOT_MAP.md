# KC02 boot chain / flash layout

Facts verified from `firmware/original.bin` plus the vendor SDK's linker script (`ax329x.ld`) and boot
sources (`ax32xx/BLDRX32.S`, `ax32xx/reset.S`, `ax32xx/spi_boot_cfg.S`). Address convention for the
**app** is `CPU = file_offset + 0x01FFDA00`; **the boot section uses a different origin** (see below).

## Flash file layout

| file range | size | contents |
|---|---|---|
| `0x000000`–`0x0001FF` | 512 B | BLDR header (below) |
| `0x000200`–`0x0002FF` | 256 B | SPI/SFC timing parameters ("SFC timing params") |
| `0x000300`–`0x0003FF` | 256 B | SFAT header (`"SFAT"` magic at `0x304`) |
| `0x000400`–`0x0025FF` | 8.5 KB | boot / exception code (1951 instructions; see below) |
| `0x002600`–`0x0D31FF` | 835 KB | application image (loaded to SDRAM `0x02000000`; `CPU = file + 0x01FFDA00`) |
| `0x0D3200`–`0x0D350F` | 784 B | SFAT asset table, 97 × 8 B (`{u32 offset, u32 size}`) |
| `0x0D3510`–`0x3071FF` | ~2.2 MB | asset data (JPEG/BMP/WAV/raw) |
| `0x307200`–`0x3FFFFF` | ~995 KB | erased (`0xFF`) |

## BLDR header (file `0x000`–`0x1FF`)

| off | value | meaning |
|---|---|---|
| `0x00` | `00 00 02 00` | first instruction of the boot chain (branch) / boot entry stub |
| `0x04` | `"BLDR"` | **magic** (checked by the updater) |
| `0x08` | `00 01 05 00` | version word (v1.5.0?) |
| `0x0C` | `0x00` | this byte is the **8-bit additive checksum slot** — `sum(first 512 B) % 256 == 0` (verified) |
| `0x10`,`0x14` | `"01234567"` ×2 | build tag string |
| `0x18` | `0x00000699` | (hypothesis) region size, see open items |
| `0x1C` | `0x0000119F` | (hypothesis) region size |
| `0x20` | **`0x02000000`** | **application load address in SDRAM** |
| `0x24` | `0x00000013` (19) | count/version field |
| `0x28` | `0x00000644` (1604) | size field |
| `0x30`, `0x34` | `0x01234567` ×2 | validity markers checked by the updater |
| `0x38` | `0x00000800` (2048) | size field |
| `0x3C` | `0x0313C00F` | packed flags/params |
| `0x1FE` | `0x55AA` | boot signature |

`tools/build.py` in the project patches only offsets `0x10-0x1ff` asset/section pointers, so this header
is otherwise untouched by theme builds. The updater's checks are minimal (magic, `0x55AA`, both
`0x01234567` markers, `SFAT` at `0x304`, additive checksum) — no CRC, no signature, no version compare.

## Boot / exception code (`file 0x400`–`0x25FF`)

* Disassembled linearly with base 0 (`or1k-elf-objdump -D -b binary -m or1k` over the extracted region) it
  is dense, coherent code: a large register-save prologue near the start
  (`l.sw 24(r1),r8 … l.sw 76(r1),r31`, then `l.mfspr r9,r0,0x20` = **EEAR** and `r0,0x40` = **EPCR**) —
  i.e. an exception entry that saves all GPRs.
* It talks only to boot-level peripherals through SPRs: `0x8838` (PCU/UART-class status), `0x8868`,
  `0x88E0` (RTC), `0x88EC` — the same register block the vendor's boot sources use.
* A **flash → SDRAM copy routine** is present around boot VMA `0x12AC`/`0x12C8`
  (`l.movhi r4,0x2000` / `l.movhi r5,0x2000` building `0x0200xxxx` addresses) — this is the loader that
  moves the application image from flash to `0x02000000` (the address the BLDR header declares at `0x20`).
* The vendor SDK's boot objects (`ax32xx_BLDRX32.S.o`, `ax32xx_reset.S.o`, `ax32xx_spi_boot_cfg.S.o`) are
  the source-level counterparts; the SDK's own linker map lists boot symbols `_start 0x320`,
  `_step_in 0x35c`, `_step_out 0x3a0`, `exception_uart_puts 0x534`, `memcpy_flash2psram 0x548`,
  `flash_to_psram 0x5a0`, `exception_init 0x6a0`, `ax32xx_vddrtcCalculate 0x76c`, `my_memset 0x798`,
  `my_putchar 0x7c8`, `myputs 0x7dc`.

### Address mapping — RESOLVED

The boot/exception section runs from on-chip SRAM with a **VMA origin at 0x0**; measured against the vendor's
own linked build (`ax329x_sdk.lst` + `ax329x_sdk.bin`, where the vendor file offset is `VMA + 0x200`):

```
our_file_offset  =  VMA + 0x1FC        (vendor build: VMA + 0x200 — a 4-byte section-size difference)
```

So the vendor listing's boot symbols translate directly onto our blob, and **all 60 boot symbols were
matched (mostly 100 % shape fidelity)**. Key entries (vendor VMA → our file offset):

| vendor VMA | symbol | our file |
|---:|---|---:|
| `0x01D4` | `_except` (common exception entry) | `0x03D0` |
| `0x0320` | `_start` | `0x051C` |
| `0x035C` / `0x03A0` | `_step_in` / `_step_out` | `0x0558` / `0x059C` |
| `0x0534` | `exception_uart_puts` | `0x0888` |
| `0x076C` | `ax32xx_vddrtcCalculate` | `0x0984` |
| `0x0798` | `my_memset` | `0x08B0` |
| `0x0818` | `dllcon_init` | `0x09B0` |
| `0x09C4` | **`sdram_init`** | `0x0B5C` |
| `0x0B40` | **`sysClkSet`** | `0x0C2C` |
| `0x0C3C` / `0x0DBC` | `sdram_tuning_check_2` / `_sdr_tuning2_conifg_set` | `0x0C88` / `0x0E08` |
| `0x19A4` | `exception_trigger` | `0x2020` |
| `0x1AC0` | `tune_values` | `0x2034` |
| `0x1C4C`–`0x1C8C` | `tune_tab_{2,1}_{dll,clk}` | `0x22C8` / `0x22E8` |
| `0x1D2C` | `tuning_test_data` | `0x23A8` |

Exception vector stubs (`_except_buserr` … `_except_breakp`) share the stub code at our file `0x0370`/`0x03C8`
and jump into `_except`.

Practical consequence: in the shared Ghidra project (base `0x01FFDA00`) the boot section lands at
`0x01FFDExx`, which is **not** its runtime address. Analysing the boot code at its real addresses therefore
uses a **separate base-0 project** with the same language (`KC02:LE:32:nodelay`).

### Base-0 boot project — DONE

Recipe (exactly reproducible):

```bash
dd if=firmware/original.bin of=boot.bin bs=1 skip=0x370 count=$((0x2600-0x370))
$GH/support/analyzeHeadless /tmp/kc02-board-re/bootv2proj bootpi -import boot.bin \
  -processor "KC02:LE:32:nodelay" -loader BinaryLoader -loader-baseAddr 0x0
```

Extracting at `file 0x370` makes the program address **equal the boot VMA** (`0x174` = the first exception
stub, since `0x174 + 0x1FC = 0x370`), so the vendor listing's boot addresses apply verbatim. A naming pass
driven by the vendor listing then produced **45 functions + 14 data labels, 0 failures**:
`_except_buserr`…`_except_breakp` (`0x174`–`0x1CC`, 8-byte jump stubs), `_except 0x1D4`, `_except_lpint 0x1E8`,
`_except_hpint 0x2C0`, `_start 0x320`, `_step_in 0x35C`, `cpy 0x380`, `_step_out 0x3A0`, `cpy_back 0x3BC`,
`exception_irtc_write 0x3F0`, `tune_values_max 0x45C`, `exception_uart_puts 0x534`,
`memcpy_flash2psram 0x548`, `flash_to_psram 0x5A0`, `exception_init 0x6A0`, `ax32xx_vddrtcCalculate 0x76C`,
`my_memset 0x798`, `my_putchar 0x7C8`, `dllcon_init 0x818`, `adcRead 0x88C`, `adcConverStart 0x8C0`,
`sdram_init 0x9C4`, `sysClkSet 0xB40`, `sdram_tuning_check_2 0xC3C`, `_sdr_tuning2_conifg_set 0xDBC`,
`exception_trigger 0x19A4`, `tune_values 0x1AC0`, `tune_tab_* 0x1C4C`–`0x1C8C`, `tuning_test_data 0x1D2C`.
This closes open item 4 below — the previously low-confidence functions are now bounded and named.
The symbol table is checked in as `docs/boot_symbols.tsv` (60 rows: address, code/data, name).

**Language fix made while doing this:** `OpenRISC.pspec` carried the *generic* OpenRISC `default_symbols`
(`or1k_reset 0x100`, `or1k_bus_error 0x200`, … `or1k_trap 0xE00`) from the upstream template. Those are a
vector layout this chip does not use (its dispatch is the stub table at `0x174`), and in a base-0 boot
import they fabricated functions on header bytes. They have been **removed** from the KC02 language file;
the real boot names come from the table above.

## Boot-time behaviour visible elsewhere in the image

* Boot-time mode selection: `file 0x2AAC` = `l.addi r3,r0,6; l.addi r4,r0,0; l.jal ui_mode_request` — the
  app requests mode 6 (main menu). `docs/CHILD_MODE.md` patches this immediate to 3 (camera mode).
* Boot-time device/UI init: `init_callbacks` table at file `0x07E000`, device list head at CPU
  `0x020C2588`, device indexes resolved from ASCII names (`ad-key` etc.) around CPU `0x02000180`–`0x020001F0`.
* SD-update filename checks live in the app (not the boot ROM): `DestBin.bin` @ file `0x07DE87`,
  `SELFTEST.bin` @ `0x07DFE5`, `exmend.bin` @ `0x07DF94`; the update *verification* loop is
  `kc02_upgrade_verify` @ CPU `0x020026C8` (`docs/BOARD_RE.md`).
* The boot loader itself exposes a USB recovery identity when the boot cable is used
  (`1908:3319`, `"BuildWin Video050Loader 1.00"` — from the sibling-camera teardown, see `docs/SDK_MAP.md`).

## Open items

1. ~~Exact link origin of the boot/exception section~~ — **resolved** (see above): VMA = file − 0x1FC, and all
   60 vendor boot symbols are mapped; the section performs SDRAM init (`sdram_init` @ our file `0x0B5C`),
   clock setup (`sysClkSet` @ `0x0C2C`), DLL/SDRAM tuning, RTC/VDD calculation and the UART exception
   printer before jumping to the application.
2. Meaning of the header size/count fields at `0x18`, `0x1C`, `0x24`, `0x28`, `0x38` (they look like
   region sizes/counts but were not correlated with the SFAT table yet).
3. Whether the `0x01234567` ASCII/"marker" pair at `0x10`/`0x14` is a build tag or a second validity check
   (the updater checks the pair at `0x30`/`0x34`).
4. ~~The remaining low-score boot matches — boundaries needed a base-0 re-analysis~~ — **resolved**:
   all 45 boot functions are now bounded and named in the base-0 project (see above), so byte matching is
   no longer needed for the boot section.
