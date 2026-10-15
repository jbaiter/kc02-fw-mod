# CSI register block, captured live during a working capture

Measured 2026-10-16 by snapshotting `0xb000`..`0xb1FC` from inside the payload, while
Luma still owned the block, into scratch RAM the stock firmware does not touch.

**Why this file matters.** The earlier reconstruction used only 13 registers captured
from stock preview. The vendor's real configuration touches far more -- all of the ISP,
dither and gamma tuning. Overriding the block with those 13 values blanked the capture
and stopped events being generated entirely. The registers marked `[Luma never wrote]`
are the ones that reconstruction lost.

81 of 128 registers are non-zero in this capture.

| reg | value | note |
|---|---|---|
| `0xb000` | `0x0440062D` | CSI mode word: type YUVR422, DVP, enabled |
| `0xb004` | `0x0000FFFF` | ISPMODE. Vendor sets 0xFFFF (bits 0-15). Luma never wrote this; adding bits 17/22 broke capture. |
| `0xb00c` | `0x00000008` | event latch: bit 3 pending at snapshot |
| `0xb010` | `0x02368980` | equals b064 |
| `0xb014` | `0x800EF87B` | interrupt enable+mask. bit 31 SET is the working value; masking it stopped all events (0x0000C00A -> 0). |
| `0xb018` | `0x00200000` | DMA state: 0x200000, arm bit 0x40000 clear = transfer settled |
| `0xb01c` | `0x02CF04FF` | sensor input size, 1280x720 |
| `0xb020` | `0x00640190` | [Luma never wrote] |
| `0xb024` | `0x02D00500` | [Luma never wrote] |
| `0xb030` | `0x05000000` | crop: sy=0, ex=1280 |
| `0xb034` | `0x02D00000` | crop: sx=0, ey=720 |
| `0xb060` | `0x02371F80` | Y plane address as the hardware holds it |
| `0xb064` | `0x02368980` | UV plane address |
| `0xb068` | `0x00000140` | byte stride 320 |
| `0xb074` | `0x000022E0` | [Luma never wrote] |
| `0xb078` | `0x02D00500` | [Luma never wrote] |
| `0xb07c` | `0x02371F80` | [Luma never wrote] |
| `0xb080` | `0x03F003F0` | [Luma never wrote] |
| `0xb084` | `0x03F003F0` | [Luma never wrote] |
| `0xb088` | `0x52000000` | [Luma never wrote] |
| `0xb08c` | `0x00501048` |  |
| `0xb090` | `0x01E05006` |  |
| `0xb094` | `0x0500C828` |  |
| `0xb098` | `0x0F176FFF` |  |
| `0xb09c` | `0x000FF3FC` |  |
| `0xb0a0` | `0x2294016C` |  |
| `0xb0a4` | `0x02000100` |  |
| `0xb0a8` | `0x04000300` |  |
| `0xb0ac` | `0x01200090` |  |
| `0xb0b0` | `0x024001B0` |  |
| `0xb0b4` | `0x11111111` |  |
| `0xb0b8` | `0x11101111` |  |
| `0xb0bc` | `0x11111111` |  |
| `0xb0c0` | `0x00000001` |  |
| `0xb0c4` | `0x0D347642` |  |
| `0xb0c8` | `0x280A0AD0` |  |
| `0xb0cc` | `0x0283C505` |  |
| `0xb0d0` | `0x1168CF19` |  |
| `0xb0d4` | `0x0283C505` |  |
| `0xb0d8` | `0x0508CF19` |  |
| `0xb0dc` | `0x1E160F08` |  |
| `0xb0e0` | `0x3C342D24` |  |
| `0xb0e4` | `0x00000100` |  |
| `0xb0e8` | `0x00040000` |  |
| `0xb0ec` | `0x10000000` |  |
| `0xb0f4` | `0x40404040` |  |
| `0xb0f8` | `0x40404040` |  |
| `0xb0fc` | `0x00022140` |  |
| `0xb100` | `0x555521B8` |  |
| `0xb104` | `0x3FFBEFBF` |  |
| `0xb108` | `0x3E04307E` |  |
| `0xb10c` | `0x3E0DA0FE` |  |
| `0xb110` | `0x3E04307E` |  |
| `0xb114` | `0x3FFBEFBF` |  |
| `0xb118` | `0x00001000` |  |
| `0xb11c` | `0x00084080` |  |
| `0xb120` | `0x01118101` |  |
| `0xb124` | `0x00084080` |  |
| `0xb128` | `0x00001000` |  |
| `0xb12c` | `0xD4FFE0A0` |  |
| `0xb130` | `0x14455555` |  |
| `0xb134` | `0x0A010000` |  |
| `0xb138` | `0x16010108` |  |
| `0xb13c` | `0x0A030000` |  |
| `0xb140` | `0x04000000` |  |
| `0xb144` | `0x08020000` |  |
| `0xb148` | `0x0C040000` |  |
| `0xb14c` | `0xC0000100` |  |
| `0xb150` | `0x21084210` |  |
| `0xb154` | `0x21084210` |  |
| `0xb158` | `0x01084210` |  |
| `0xb15c` | `0xA0808080` |  |
| `0xb160` | `0x403A3020` |  |
| `0xb164` | `0x44444444` |  |
| `0xb168` | `0x14A5294A` |  |
| `0xb16c` | `0x14A5294A` |  |
| `0xb170` | `0x00A5294A` |  |
| `0xb174` | `0x01140A04` |  |
| `0xb178` | `0x03C00140` |  |
| `0xb17c` | `0x021C00B4` |  |
| `0xb180` | `0x00B40140` | scaler target 320x180 |

## The two findings that matter most

1. **`b184`/`b188` are ZERO in a working capture.** The previous session noted Luma
   omitted the scaler ratios and suggested that caused shearing. In the vendor's own
   working configuration both are zero, so that hypothesis was wrong: the ratios are
   not used on this path, and writing `0x80000` to them was a change in the wrong
   direction.

2. **`b014` bit 31 must stay SET.** Clearing it stopped event generation completely --
   `events_seen` went from `0x0000C00A` (45 polls) to `0x00000000` (0 polls) over 4M
   polls. The stock reads `0x0000F818` with bit 31 clear in USB mode, but that is a
   mode where the CSI is idle; it is not a configuration to copy while capturing.

## Also worth keeping

- `b060`/`b064` read back as the values Luma wrote PLUS 0xE0. The register is not a
  plain base address, or the hardware advances it. Do not assume a read-back will
  equal what was written.
- `b004` is ISPMODE for the whole ISP, not a CSI-only field. Treat it as the vendor's.
