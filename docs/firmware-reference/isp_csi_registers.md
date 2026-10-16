# The CSI / ISP / line-scaler register block (SFR6, 0xb000-0xb1E8)

Provenance: register NAMES and bit fields are from the vendor SDK
(`ax32xx/inc/ax329x.h` for the block map, `ax32xx_csi.h` for the bit enums, plus
`ax32xx/src/ax32xx_isp.c` and `ax32xx/csi/ax32xx_csi.c` for how they are driven).
VALUES in the right-hand column are measured live from a WORKING capture --
snapshotted inside the payload while Luma owned the block (see csi-registers.md).

Everything below is SDK-documented, not inferred. That is the point of this file:
the earlier register work was reconstructed from a 13-register sample and was wrong
in ways that cost a full session. **Read the SDK header first.**

## Two names that mislead

- **`LS` means LINE SCALER, not lens shading.** `ISP_LS_SIZE/HSTP/VSTP` and the
  `ISP_LS_HTAB/VTAB` tables are the scaler's target, step and anti-lsawtooth
  tables. `ISP_MODE_LSEN` (ISPMODE bit 22) enables it.
- **`I2L` means ISP-to-LCD**, the path the viewfinder uses: `CSI_I2L_YBADR`,
  `CSI_I2L_UVBADR`, `CSI_I2L_STRD`, `CSI_I2L_HCROP/VCROP`. The parallel `I2J`
  (ISP-to-JPEG) and `I2W` (ISP-to-WiFi) sets are separate and unused here.

## Block map

| addr | SDK name | measured | notes |
|---|---|---|---|
| `0xb000` | `SFR_6_TEST / CSIMODE alias` | `0x0440062D` | CSI mode word. b000=0x0440062D in all working captures. |
| `0xb004` | `ISPMODE` | `0x0000FFFF` | ISPMODE enable bitmask -- see the table below. Working value 0x0000FFFF. |
| `0xb008` | `ISPCTRL` | 0 | ISPCTRL kickstart/clear bits -- see below. bit 19 = LS_KS. |
| `0xb00c` | `CSI_INTR` | `0x00000008` | CSI_INTR: event latch, write-1-to-clear. See the event table below. |
| `0xb010` | `CSI_I2L_RUVBADR` | `0x02368980` |  |
| `0xb014` | `CSI_INTEN` | `0x800EF87B` | CSI_INTEN: interrupt enable + mask. bit 31 SET is required; clearing it stopped event generation entirely. |
| `0xb018` | `CSI_DMA_MODE` | `0x00200000` | CSI_DMA_MODE: output routing and arm bits -- see below. |
| `0xb01c` | `CSI_IMAGE_SIZE` | `0x02CF04FF` | CSI_IMAGE_SIZE: sensor input geometry. 0x02CF04FF = 1280x720. |
| `0xb020` | `CSI_TEST_BLANK` | `0x00640190` |  |
| `0xb024` | `CSI_TEST_SIZE` | `0x02D00500` |  |
| `0xb028` | `CSI_I2J_HCROP` | 0 |  |
| `0xb02c` | `CSI_I2J_VCROP` | 0 |  |
| `0xb030` | `CSI_I2L_HCROP` | `0x05000000` | CSI_I2L_HCROP: LCD crop horizontal (ex<<16)|sx. |
| `0xb034` | `CSI_I2L_VCROP` | `0x02D00000` | CSI_I2L_VCROP: LCD crop vertical (ey<<16)|sx. |
| `0xb038` | `CSI_I2W_HCROP` | 0 |  |
| `0xb03c` | `CSI_I2W_VCROP` | 0 |  |
| `0xb040` | `CSI_I2W_YBADR` | 0 |  |
| `0xb044` | `CSI_I2W_UVBADR` | 0 |  |
| `0xb048` | `CSI_I2W_LBC` | 0 |  |
| `0xb04c` | `CSI_I2W_LBS` | 0 |  |
| `0xb050` | `CSI_I2J_YBADR` | 0 |  |
| `0xb054` | `CSI_I2J_UVBADR` | 0 |  |
| `0xb058` | `CSI_I2J_LBC` | 0 |  |
| `0xb05c` | `CSI_I2J_LBS` | 0 |  |
| `0xb060` | `CSI_I2L_YBADR` | `0x02371F80` | CSI_I2L_YBADR: the Y plane address handed to the LCD DMA. |
| `0xb064` | `CSI_I2L_UVBADR` | `0x02368980` | CSI_I2L_UVBADR: the chroma plane address. |
| `0xb068` | `CSI_I2L_STRD` | `0x00000140` | CSI_I2L_STRD: byte stride, not words (unlike the DE stride). |
| `0xb06c` | `CSI_UVC_LADR` | 0 |  |
| `0xb070` | `CSI_DVP_BADR` | 0 |  |
| `0xb074` | `CSI_STAT_BADR` | `0x000022E0` |  |
| `0xb078` | `CSI_SIZE_STAT` | `0x02D00500` |  |
| `0xb07c` | `CSI_I2L_RYBADR` | `0x02371F80` | CSI_I2L_RYBADR: Y read address -- read back as b060 + 0xE0. |
| `0xb080` | `ISP_BLC_PARA0` | `0x03F003F0` |  |
| `0xb084` | `ISP_BLC_PARA1` | `0x03F003F0` |  |
| `0xb088` | `ISP_DPC_PARA0` | `0x52000000` |  |
| `0xb08c` | `ISP_DPC_PARA1` | `0x00501048` |  |
| `0xb090` | `ISP_DPC_PARA2` | `0x01E05006` |  |
| `0xb094` | `ISP_DPC_PARA3` | `0x0500C828` |  |
| `0xb098` | `ISP_DPC_PARA4` | `0x0F176FFF` |  |
| `0xb09c` | `ISP_AWB_PARA0` | `0x000FF3FC` |  |
| `0xb0a0` | `ISP_AWB_GAIN` | `0x2294016C` |  |
| `0xb0a4` | `ISP_AE_WINX01` | `0x02000100` |  |
| `0xb0a8` | `ISP_AE_WINX23` | `0x04000300` |  |
| `0xb0ac` | `ISP_AE_WINY01` | `0x01200090` |  |
| `0xb0b0` | `ISP_AE_WINY23` | `0x024001B0` |  |
| `0xb0b4` | `ISP_AE_WEIGHT0` | `0x11111111` |  |
| `0xb0b8` | `ISP_AE_WEIGHT1` | `0x11101111` |  |
| `0xb0bc` | `ISP_AE_WEIGHT2` | `0x11111111` |  |
| `0xb0c0` | `ISP_AE_WEIGHT3` | `0x00000001` |  |
| `0xb0c4` | `ISP_WP_PARA0` | `0x0D347642` |  |
| `0xb0c8` | `ISP_WP_PARA1` | `0x280A0AD0` |  |
| `0xb0cc` | `ISP_WP_PARA2` | `0x0283C505` |  |
| `0xb0d0` | `ISP_WP_PARA3` | `0x1168CF19` |  |
| `0xb0d4` | `ISP_WP_PARA4` | `0x0283C505` |  |
| `0xb0d8` | `ISP_WP_PARA5` | `0x0508CF19` |  |
| `0xb0dc` | `ISP_WP_PARA6` | `0x1E160F08` |  |
| `0xb0e0` | `ISP_WP_PARA7` | `0x3C342D24` |  |
| `0xb0e4` | `ISP_CCM_PARA0` | `0x00000100` |  |
| `0xb0e8` | `ISP_CCM_PARA1` | `0x00040000` |  |
| `0xb0ec` | `ISP_CCM_PARA2` | `0x10000000` |  |
| `0xb0f0` | `ISP_CCM_PARA3` | 0 |  |
| `0xb0f4` | `ISP_DGAIN_PARA0` | `0x40404040` |  |
| `0xb0f8` | `ISP_DGAIN_PARA1` | `0x40404040` |  |
| `0xb0fc` | `ISP_DGAIN_PARA2` | `0x00022140` |  |
| `0xb100` | `ISP_SHARP_PARA` | `0x555521B8` |  |
| `0xb104` | `ISP_SHARP_EEM0` | `0x3FFBEFBF` |  |
| `0xb108` | `ISP_SHARP_EEM1` | `0x3E04307E` |  |
| `0xb10c` | `ISP_SHARP_EEM2` | `0x3E0DA0FE` |  |
| `0xb110` | `ISP_SHARP_EEM3` | `0x3E04307E` |  |
| `0xb114` | `ISP_SHARP_EEM4` | `0x3FFBEFBF` |  |
| `0xb118` | `ISP_SHARP_DNM0` | `0x00001000` |  |
| `0xb11c` | `ISP_SHARP_DNM1` | `0x00084080` |  |
| `0xb120` | `ISP_SHARP_DNM2` | `0x01118101` |  |
| `0xb124` | `ISP_SHARP_DNM3` | `0x00084080` |  |
| `0xb128` | `ISP_SHARP_DNM4` | `0x00001000` |  |
| `0xb12c` | `ISP_CCF_PARA` | `0xD4FFE0A0` |  |
| `0xb130` | `ISP_CCF_CDM` | `0x14455555` |  |
| `0xb134` | `ISP_CH_PARA0` | `0x0A010000` |  |
| `0xb138` | `ISP_CH_PARA1` | `0x16010108` |  |
| `0xb13c` | `ISP_CH_PARA2` | `0x0A030000` |  |
| `0xb140` | `ISP_CH_PARA3` | `0x04000000` |  |
| `0xb144` | `ISP_CH_PARA4` | `0x08020000` |  |
| `0xb148` | `ISP_CH_PARA5` | `0x0C040000` |  |
| `0xb14c` | `ISP_CH_PARA6` | `0xC0000100` |  |
| `0xb150` | `ISP_CH_PARA7` | `0x21084210` |  |
| `0xb154` | `ISP_CH_PARA8` | `0x21084210` |  |
| `0xb158` | `ISP_CH_PARA9` | `0x01084210` |  |
| `0xb15c` | `ISP_VDE_PARA0` | `0xA0808080` |  |
| `0xb160` | `ISP_VDE_PARA1` | `0x403A3020` |  |
| `0xb164` | `ISP_VDE_PARA2` | `0x44444444` |  |
| `0xb168` | `ISP_SAJ_PARA0` | `0x14A5294A` |  |
| `0xb16c` | `ISP_SAJ_PARA1` | `0x14A5294A` |  |
| `0xb170` | `ISP_SAJ_PARA2` | `0x00A5294A` |  |
| `0xb174` | `ISP_MD_PARA0` | `0x01140A04` |  |
| `0xb178` | `ISP_MD_PARA1` | `0x03C00140` |  |
| `0xb17c` | `ISP_MD_PARA2` | `0x021C00B4` |  |
| `0xb180` | `ISP_LS_SIZE` | `0x00B40140` | ISP_LS_SIZE: line scaler target size. |
| `0xb184` | `ISP_LS_HSTP` | 0 | ISP_LS_HSTP: line scaler horizontal step (the "ratio"). |
| `0xb188` | `ISP_LS_VSTP` | 0 | ISP_LS_VSTP: line scaler vertical step. |
| `0xb18c` | `ISP_LS_HTAB0` | 0 | ISP_LS_HTAB0: start of the line-scaler horizontal table. |
| `0xb190` | `ISP_LS_HTAB1` | 0 |  |
| `0xb194` | `ISP_LS_HTAB2` | 0 |  |
| `0xb198` | `ISP_LS_HTAB3` | 0 |  |
| `0xb19c` | `ISP_LS_HTAB4` | 0 |  |
| `0xb1a0` | `ISP_LS_HTAB5` | 0 |  |
| `0xb1a4` | `ISP_LS_HTAB6` | 0 |  |
| `0xb1a8` | `ISP_LS_HTAB7` | 0 |  |
| `0xb1ac` | `ISP_LS_HTAB8` | 0 |  |
| `0xb1b0` | `ISP_LS_HTAB9` | 0 |  |
| `0xb1b4` | `ISP_LS_HTAB10` | 0 |  |
| `0xb1b8` | `ISP_LS_HTAB11` | 0 |  |
| `0xb1bc` | `ISP_LS_HTAB12` | 0 |  |
| `0xb1c0` | `ISP_LS_HTAB13` | 0 |  |
| `0xb1c4` | `ISP_LS_HTAB14` | 0 |  |
| `0xb1c8` | `ISP_LS_HTAB15` | 0 |  |
| `0xb1cc` | `ISP_LS_VTAB0` | 0 |  |
| `0xb1d0` | `ISP_LS_VTAB1` | 0 |  |
| `0xb1d4` | `ISP_LS_VTAB2` | 0 |  |
| `0xb1d8` | `ISP_LS_VTAB3` | 0 |  |
| `0xb1dc` | `ISP_LS_VTAB4` | 0 |  |
| `0xb1e0` | `ISP_LS_VTAB5` | 0 |  |
| `0xb1e4` | `ISP_LS_VTAB6` | 0 |  |
| `0xb1e8` | `ISP_LS_VTAB7` | 0 |  |
| `0xb1ec` | `(unnamed)` | 0 |  |
| `0xb1f0` | `(unnamed)` | 0 |  |
| `0xb1f4` | `(unnamed)` | 0 |  |
| `0xb1f8` | `(unnamed)` | 0 |  |
| `0xb1fc` | `(unnamed)` | 0 |  |

## ISPMODE (0xb004) bits -- `ISP_MODE_E`

| bit | name | | bit | name |
|---|---|---|---|---|
| 0x00000001 | BLCEN  black level | | 0x00010000 | JPEGCROP |
| 0x00000002 | LSCEN  lens shading | | 0x00020000 | LCDCROP |
| 0x00000004 | DPCEN  dead pixel | | 0x00040000 | WIFICROP |
| 0x00000008 | AWBEN  white balance | | 0x00080000 | SDEN |
| 0x00000010 | CFAEN  colour filter | | 0x00100000 | CSI_SAVEEN |
| 0x00000020 | CCMEN  colour matrix | | 0x00200000 | JSCALER_SEL |
| 0x00000040 | DGAINEN digital gain | | 0x00400000 | LSEN  LINE SCALER |
| 0x00000080 | YGAMMAEN | | | |
| 0x00000100 | RGBGAMMAEN | | | |
| 0x00000200 | CHEN | | | |
| 0x00000400 | VDEEN | | | |
| 0x00000800 | SHARPEN | | | |
| 0x00001000 | CCFEN | | | |
| 0x00002000 | SAJEN | | | |
| 0x00004000 | AE_STATEN | | | |
| 0x00008000 | AWB_STATEN | | | |

**MEASURED WORKING VALUE: `0x0000FFFF`** -- bits 0-15 only, i.e. BLCEN through
AWB_STATEN. `LCDCROP` and `LSEN` are NOT set in a working capture. An earlier
change enabled both (| 0x20000, | 0x400000) on the theory that the scaler needed
them; that broke capture. The measurement and the vendor both say no.

## ISPCTRL (0xb008) bits -- kickstart and clear

| bit | name | meaning |
|---|---|---|
| 0x00000001 | AWB_STAT_CLR | clear AWB statistics |
| 0x00000002 | AE_STAT_CLR | clear AE statistics |
| 0x00000004 | CP_STAT_KS | kickstart CP statistics |
| 0x00010000 | AWB_TAB_KS | kickstart AWB table load |
| 0x00020000 | YGAMMA_KS | kickstart Y gamma table load |
| 0x00040000 | RGAMMA_KS | kickstart RGB gamma table load |
| 0x00080000 | LS_KS | **kickstart LINE SCALER table load** |

`LS_KS` is the bit this project wrote as `b008 = 1<<19` and called "the latch".
That name was right by accident: it is the line-scaler kickstart, and a table load
or scaler reprogram is not complete until it is raised.

## CSI_DMA_MODE (0xb018) bits -- `CSI_OUTPUT_*`

| bit | name | meaning |
|---|---|---|
| 0x00003FFF | UVCSIZE | UVC packet size field |
| 0x0000C000 | UVCPRIO | UVC priority |
| 0x00010000 | UVCEN | UVC output enable |
| 0x00020000 | RELOAD | reload the LCD transfer |
| 0x00040000 | **LCDEN** | **LCD DMA output enable -- the bit this project calls "arm"** |
| 0x00080000 | MJPGEN | MJPEG output enable |
| 0x00100000 | WIFIEN | WiFi output enable |
| 0x00200000 | **LDMAMANEN** | **LCD DMA MANUAL mode enable** |

`LCDEN` is the "arm bit" that self-clears on completion, which is why the transfer
needs an explicit re-kick. Measured working value `b018 = 0x00240000` = LDMAMANEN
| LCDEN, i.e. manual mode with the output enabled.

Audit, 2026-10-16: the normal frame callback at `0x0203a2f0` rewrites both
Y/UV addresses before calling `csiLCDDmaKick` (OR LCDEN). It does not clear
LCDEN or reset PIP on normal completion. The earlier automatic-mode experiment
wrote `0x20000` without LCDEN, so its empty output does **not** establish that
manual mode is required. See `lcd_frame_callback.c`.

## CSI_INTR (0xb00c) event bits -- `CSI_IRQ_*`

This is an INDEX enum, so bit N is (1<<N).

| bit | mask | name |
|---|---|---|
| 0 | 0x0001 | JPG_FRAME_END |
| 1 | **0x0002** | **LCD_FRAME_END -- the correct re-arm trigger** |
| 2 | 0x0004 | UVC_PACKE_END |
| 3 | 0x0008 | SEN_STATE_INT |
| 4 | 0x0010 | SEN_STATE_ERR  (NOT a frame event) |
| 5 | 0x0020 | JBF_DMAWR_ERR |
| 6 | 0x0040 | LBF_DMAWR_ERR |
| 7 | 0x0080 | MDT_MOTION_DET |
| 8 | 0x0100 | J1BUFF_ERR |
| 9 | 0x0200 | VXY_FRAME_EN |
| 10 | 0x0400 | ISP |
| 17 | 0x020000 | CSI_ERR |
| 18 | 0x040000 | LB_OVERFLOW |
| 31 | 0x80000000 | (INT_EN, not an event) |

### This is the important correction

The stock handler re-arms inside a guard on event bit **4 (0x10)**, which is
`SEN_STATE_ERR` -- a SENSOR STATE ERROR, and the surrounding stock guard
`events & 0xE0970` is a set of ERROR bits. So the vendor's re-arm is an
ERROR-RECOVERY path, not the normal per-frame path. This project copied it as if
it were the frame trigger, which is why re-arming was unreliable: `0x10` is present
in some runs and absent in others (measured `0x1A` versus `0xC00A`).

The frame-end event is bit **1 (`0x0002`, `CSI_IRQ_LCD_FRAME_END`)** -- and it was
present in EVERY event set measured: 0x000A, 0x001A, 0xC00A, 0xC00A. The correct
per-frame trigger is therefore `events & 0x0002`, not `events & 0x0010`.

## Historical configuration (produced an image but did not establish safe DMA bounds)

The later capture audit found pixel data in scalars following the capture arrays.
In this snapshot LCDCROP and LSEN are off. Do not copy it as a safe capture recipe.
The guarded run in `docs/hw/2026-10-16-capture-audit.md` instead calls the actual
stock scaler: b004 becomes **0x42ffff**, b184/b188 become **0x80000**, and its
45-frame bounded test leaves DMA guards intact. Live panel verification is pending.

```
b000 = 0x0440062D   CSI enabled; FORMAT bits 7:6 = 0 (raw), not YUVR422
b004 = 0x0000FFFF   ISP: BLC..AWB_STAT enabled; LCDCROP and LSEN OFF
b00c = 0x00000000   no event pending at snapshot time
b014 = 0x800EF87B   interrupt enable+mask; bit 31 SET
b018 = 0x00240000   LDMAMANEN | LCDEN (manual mode, output enabled)
b01c = 0x02CF04FF   sensor input 1280x720
b030 = 0x05000000   LCD crop H: ex=1280, sx=0
b034 = 0x02D00000   LCD crop V: ey=720
b060 = 0x02371F80   Y plane address (Luma's buffer + 0xE0)
b064 = 0x02368980   UV plane address (Luma's buffer + 0xE0)
b068 = 0x00000140   stride 320 bytes
b180 = 0x00B40140   line scaler target
b184 = 0x00000000   line scaler H step
b188 = 0x00000000   line scaler V step
```

81 of the 128 registers in 0xb000-0xb1FC are non-zero in this capture. The ones
this project never wrote are the ISP tuning: BLC, DPC, AWB, AE windows and weights,
WP, CCM, DGAIN, SHARP, CCF, CH, VDE, SAJ, MD and the LS tables. Overwriting that
set with a 13-register reconstruction blanked capture outright.

## Traps, restated

1. `b014` bit 31 must stay SET. Clearing it stopped event generation entirely:
   measured `events_seen` 0x0000C00A -> 0x00000000 across 4,000,000 polls.
2. `b004` is the whole ISP enable mask, not a scaler field. Do not OR bits into it.
3. `b008` bit 19 (`LS_KS`) is a kickstart, not a configuration bit.
4. `b00c` is write-1-to-clear. Writing a whole value back can clear events that
   arrived since the read.
5. The line scaler tables (b18c-b1e8) are loaded once behind a kickstart; they are
   NOT scaler ratios as this project once assumed.
