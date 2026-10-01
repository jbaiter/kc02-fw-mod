# KC02 image → print/display pipeline — reference map

Target: **HiMont KC02 (U8)**, `original_firmware_backup.bin`
(md5 `7236c4c1bd3f12b1d85d33a9bebdf510`). Companion to
`UI_STATE_MACHINE.md`. Conventions identical: CPU addresses
(`CPU = 0x02000000 + fileoff - 0x2600`), CONFIRMED = listing/raw bytes quoted,
INFERRED = consistent but unproven. Symbols replayable from
`tools/ghidra_symbols.tsv`.

## 1. The one-sentence answer

**There is no CPU-side dither/threshold code.** The image gets an artistic
*stylize* pass in software (gradient-shaded LUT, optionally flattened to
grayscale), then the tone→output conversion is done by a **hardware
scaler/formatter engine (SPR block `0xC8xx`)** with programmable polyphase
coefficients and two DMA output channels. "Dithered variant" vs
"continuous/grayscale" is (a) the stylize stage's look + chroma handling and
(b) per-channel hardware configuration — not two software encoders.

## 2. Capture flow (camera mode 3, shutter events `0x1a`/`0x23`)

`key_mode3_shutter_fsm` **`0x0200A4BC`** — self-timer/copies FSM:

| `ui_shutter_mode` `0x020D41C7` | behavior |
|---|---|
| 0 / 1 / 2 | 1 / 3 / 5 copies (burst loop in `r14`) |
| 3 / 4 / 5 | self-timer 3 s / 5 s / 10 s (1 s countdown via `tick_get` delta ≤ 999) |
| 6 | 5 s timer + 3 copies |

Per iteration: `frame_current_get 0x02022720` → `img_stylize_gradient_lut
0x02022434` → `frame_submit_display_or_queue 0x02039E44` (preview) →
`media_busy_get 0x020524A0` wait → `0x02004588` (INFERRED print-page submit;
paper/lid `hal_sense_read` reg `0x0200000A` follows it) → save JPEG via
`kc02_file_open` → `0x0205ADF0` (status 8/9/14 → toasts `0x56/0x57/0xBB`).
SFX #`0x2C` plays through `sfx_play_asset 0x020071C4`.

## 3. The stylize stage — the "dithered/printed look" (CONFIRMED)

`img_stylize_gradient_lut` **`0x02022434(Y, UV, w, h, flag)`**, 8-bit Y + UV
planes, output in place:

1. Copy Y (`malloc(w*h)`, `kc02_memcpy`).
2. Per pixel: quantize neighbors `>>3`; horizontal & vertical **gradient terms**
   with edge-aware weights (interior `(3·far + near)/4`, simplified at borders);
   each biased **+31** (range 0..62).
3. Index **`print_style_lut_63x63` `0x0207C640`** (3969 × u32,
   file `0x7EC40`) at `(gy)*63 + gx`; write `(entry << 3)` as the new pixel.
   The table is a smooth asymmetric pyramid: **31 at flat center (→248 white),
   0 at gradient extremes (→black)** — inverse-gradient shading =
   line/relief sketch rendering. **The entire print art style is this one
   editable table.**
4. Flag fork: `flag==0` → `memset(UV, 0x80, w*h/2)` (neutral chroma =
   **grayscale** output); else chroma kept (tinted variant). Flag =
   `(self-timer-mode != 7)` at the capture call site.
5. `dcache_invalidate_range` both planes (the output DMA consumes them).

## 4. Output paths — compositor vs hardware channels

Two parallel output mechanisms exist (both CONFIRMED):

**UI compositor (software):** `frame_submit_display_or_queue 0x02039E44`
(gate `desc+0x2a == 4`) → `compositor_submit_rect 0x02044560` (3-slot queue at
`0x020D1ADC`, IRQ-masked via SPR `0x18`) → `compositor_blit_rect 0x02044CE0` /
direct `compositor_commit 0x02044378`. Handles widget/UI layer rectangles
(desc `+0x20..+0x26` = x,y,w,h).

**Hardware scaler/formatter (0xC8xx engine):** framebuffers from
`framebuffer_get 0x02004840(id)` (ids 1/2; 320×240 = `0x12C00` bytes) go to
`framebuffer_submit_to_channel 0x020048F8(id, fb)` → `output_channel_submit
0x0203B6B8` → channel table **`0x020CE9AC`** (2 × 32 B entries: `+0x10` ctx,
`+0x1c` current fb) → `output_hw_program_dma 0x02033C8C`:

```
ch 1: SPR 0xC83C (size regs) … mtspr 0xC844 ← fb ; 0xC848 ← size-1
ch 0: SPR 0xC84C (size regs) … mtspr 0xC854 ← fb ; 0xC858 ← size-1
```

Sizes computed from the scaler dimension SPRs as `(lo16+1)·(hi16+1)`.
`0x02033D98` selects per-channel mode bits in SPR `0xC85C` (CONFIRMED).
Channel `0x02033E04` (**`output_hw_load_coeffs`**) loads a coefficient block
into SPRs `0xC880..0xC8B0+`: default `output_default_coeffs 0x020814F0`
(**Q30 identity rows + Q6 polyphase resampler taps** `[-5,62,8,-1]`,
`[-8,58,18,-4]`, `[-9,49,30,-6]`, …), or a caller-supplied table. Callers:
`0x0203AD48`, `0x02039448` (path A/B setups — display vs second output,
INFERRED assignment).

**Negative results (searched, zero hits):** CPU-side Floyd–Steinberg/ordered
threshold loops on the whole capture→stylize→submit path; 4×4/8×8 Bayer
matrices in u8 and u32 form; `0x020814F0` and neighbours are resampler
coefficients, not dither matrices. So per-tone-dot generation is inside the
0xC8xx engine / the head controller, configured by the register block — any
"dithered vs grayscale" hardware difference is a **coefficient + mode-register
configuration** chosen by the two setup callers.

## 5. What is NOT print (corrections)

`media engine 0x02052xxx` + `0x02056F28` format fork + the 44/60/90/58-byte
blocks copied to `0x020D1DA4` are the **AUDIO player** (evidence: blocks are
`RIFF/WAVE/fmt ` headers with fmt-chunk sizes 0x14/0x32/0x10; depths 16/4/8 =
PCM/ADPCM; `0x020525B4` clamps 0..100 = **volume**). Earlier notes calling
this the "burn formatter" are RETRACTED. Playback OK (`0x1A` →
`key_mode4_ok_save_to_sd 0x0200C24C`) is **save-to-SD**, not print. Event
`0x0F` (`0x0200B8CC`) fills a 320×240 fb with `0xF9`, cleans, submits channel 1
= **paper feed** (solid-white print) — INFERRED but strongly consistent.

## 6. Entry points and modal

| Trigger | Handler | Meaning |
|---|---|---|
| camera `0x1a`/`0x23` | `0x0200A4BC` | capture+self-timer/copies FSM (§2) |
| playback `0x1a`/`0x23` | `0x0200C24C` | save current photo to SD |
| playback `0x0f` | `0x0200B8CC` | fill+submit ch1 (paper feed) |
| playback `0x10` | `0x0200BDB0` | open modal `0x020C433C` |
| modal `0x1a`/`0x23` | `0x02011A34` | confirm → `ui_mode_request` + file ops |
| camera `0x24` | `0x02009F08` | request main menu (child-locked) |

`0x02004588` (shutter tail, args `(0)`) is the INFERRED print-page submit —
first callee chain worth finishing if the exact head-queue semantics matter.

## 7. Customization points

| Goal | Where | Kind |
|---|---|---|
| Change print art style (sketch/relief curve) | LUT `0x0207C640` (file `0x7EC40`, 3969 u32, `×8` output) | **data table** |
| Grayscale vs tinted look | stylize flag arg (call site `0x0200A6B8`; mode-7 rule) | parameter |
| Copies / self-timer | `0x020D41C7` FSM states | RAM byte / menu setting |
| Resampling sharpness | coeff tables via `0x02033E04` (`0x020814F0` default + caller tables) | data tables |
| Output format per channel | SPR `0xC85C` mode bits via `0x02033D98`, geometry via `0x02033C8C` | hardware regs |

## 8. Open questions

- Head-side burn sequencing (strobe/motor) not traced below the 0xC8xx
  register layer; no stepper/strobe GPIO loop has been located yet — it is
  likely interrupt-driven from the channel-1 DMA completion.
- Path A/B assignment (`0x0203AD48` vs `0x02039448`) to display vs print is
  INFERRED.
- `0x02004588` internals untraced (§6).
- Modal `0x020C433C` purpose (print-count vs delete confirm) INFERRED; its
  confirm does `ui_mode_request` + file ops.

## CORRECTION (2026-10-02, hardware-validated)

The `flag` parameter and stylize gating, pinned live on hardware:

- The stylize runs ONLY when `byte[0x020D41C7]` (shutter-FSM state) ∈ **{7, 8}**
  — the print-style capture states. Ordinary captures (values 0–6) never
  stylize; what prints then is the printer's own halftone.
- `flag = (byte == 8)`: `0` → UV plane flattened to 0x80 (grayscale);
  `1` → chroma kept. The LUT store in the gradient loop is unconditional.
- The 63×63 table at 0x0207C640 is a real asset (asymmetric pyramid, 764
  nonzero entries), and `img_stylize_gradient_lut` is its only accessor in the
  firmware. Swapping the table = full print-style control (validated: inverted
  "chalkboard" style printed).
- The camera's "dithered / continuous grayscale" setting is NOT this byte —
  it selects the printer's halftone mode.
