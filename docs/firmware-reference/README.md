# Stock firmware reference — decompiled subsystems

Clean C transcriptions of the original KC02 firmware routines that Luma needs,
kept in the repo so a subsystem is read once instead of re-derived every session.

**Provenance.** Every function here was decompiled from `firmware/original.bin`
(SHA-256 `218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5`) with
Ghidra, then hand-cleaned: renamed locals, restored the SDK's register names and
control flow, and reduced `CONCAT22`-style artifacts. The function address is kept
above each body so it can be re-checked against the binary.

**What "clean" does not mean.** These are transcriptions, not recovered sources.
Where a body is ambiguous, the ambiguity is stated rather than smoothed over. Any
line marked `VERIFIED` was confirmed against measured hardware behaviour in this
session; an unmarked line is decompiler output only.

**Why the addresses matter.** A RAM run can call these directly, because the stock
code is still in memory when Luma runs. That is how the sensor and CSI bring-up
were fixed: not by reimplementing the vendor logic, but by calling it or copying
its exact sequence.

## Files

| file | contents |
|---|---|
| `sensor_iic.c` | the bit-banged I²C bus, the autodetect/init path, and the exposure writer |
| `csi.c` | CSI mode/type/enable, the LCD framebuffer, the scaler, the DMA, and the frame interrupt |
| `display.c` | DE video layer and LCD frame servicing |
| `power_clock_gpio.c` | LDO rails, module clock gates, GPIO pin muxing |
| `adc_keypad.c` | the ADC used for the button ladder |
| `rotate_pip.c` | the hardware rotation engine and the pip counter |
| `jpeg_capture.c` | the still-photo path: manual vs streaming encode, the I2W frame DMA, and the encoder register contract |

## Traps found the hard way

Recorded here because each one cost real time and none is obvious from a header:

1. **The sensor is not on a hardware I²C controller.** `sensor_iic_*` wrap
   `software_iic_*`, i.e. GPIO bit-banging, because this build takes the SDK's
   `USE_SOFTWARE_IIC0` path. Driving IIC0/IIC1 is driving pins nothing is wired to.
2. **The sensor's 8-bit address is `0x80`, not `0x40`.** Measured, and it
   contradicts both the emulator model and the stock helper's call site — which is
   why `hal_iic08bitAddrReadData(0x40,...)` reads `0xff` on silicon.
3. **Module clocks are gated, and a cleared gate bit means enabled.**
   `ax32xx_sysClkSet(type, 1)` *clears* a bit in `0x8840`/`0x8844`. Both the ADC
   and the CSI were dead until their gate was cleared, and both looked like
   "half-working peripherals" rather than "powered-off blocks".
4. **`ax32xx_sysLDOSet` only implements indices 0..5.** An index outside that range
   falls to the default branch and returns silently, so a wrong LDO index is a
   silent no-op rather than an error.
5. **A DE commit is frame-gated.** Writing the framebuffer address does nothing
   until a frame boundary, which requires the LCD frame event to be acknowledged
   *and* the panel kicked.
6. **The CSI re-arm is three steps, not one.** Clear the bit, reset the pip
   counter, then set it. Setting the bit alone does not restart a finished
   transfer.
7. **The CSI input format is `CSI_TYPE_YUVR422`** — YUV 4:2:2 in *video* range
   (Y 16..235, chroma 16..240). Treating it as full-range 4:2:0 both displaces the
   chroma and washes out the colour.
