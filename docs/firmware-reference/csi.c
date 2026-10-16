/* Stock KC02 firmware reference: the CSI capture block.
 *
 * Decompiled from firmware/original.bin. See README.md for provenance and for the
 * traps this file documents. The frame interrupt is the important part: it is the
 * servicing Luma was missing, and it explains why capture was intermittent.
 *
 * AUDIT 2026-10-16: historical explanations below conflate ERROR recovery with
 * normal frame completion. See lcd_frame_callback.c: normal event 1 dispatches
 * hal_lcdSwitchFrame2 (0x0203a2f0), which writes Y/UV addresses even on reuse,
 * then calls csiLCDDmaKick (OR LCDEN only). No normal-frame PIP reset. The
 * automatic-mode test omitted LCDEN, so it does not prove manual mode mandatory.
 * See docs/hw/2026-10-16-capture-audit.md for the later memory-corruption finding:
 * b004=0xffff left scaling OFF. Calling csiLCDScaler produced b004=0x42ffff and
 * 45 bounded frames with intact DMA guards. The user then confirmed live colour
 * capture without the shrinking bar (guarded-panel-150413). FPS/layout remain.
 */
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define REG(n) (*(volatile u32 *)(n))

/* ---------------------------------------------------------------------------
 * CSI type, from ax32xx/inc/ax32xx_csi.h.  b000 carries these in two fields.
 * ------------------------------------------------------------------------ */
enum {
    CSI_TYPE_RAW8       = 0,
    CSI_TYPE_RAW10      = 1,
    CSI_TYPE_RAW12      = 2,
    CSI_TYPE_YUV422     = 3,
    CSI_TYPE_YUVR422    = 4,     /* YUV 4:2:2 input, VIDEO range. Not b000=0x0440062d. */
    CSI_TYPE_COLOR_BAR  = 0xff,
};

#define CSI_MODE_ENABLE       0x00000001u
#define CSI_MODE_FORMAT       0x000000C0u   /* bits 7:6   */
#define CSI_MODE_DATA_BW      0x00000C00u   /* bits 11:10 */

/* 0x02034600  ax32xx_csiTypeSet(type)
 * Clears both the FORMAT and DATA_BW fields, then puts types 0..2 in DATA_BW
 * (the raw data widths) and types >= 3 in FORMAT as (type-1)&3.
 *
 * Decoding b000 = 0x0440062d gives FORMAT bits 7:6 = 0 and DATA_BW bits
 * 11:10 = 1, consistent with RAW10. An earlier transcription incorrectly
 * claimed bits 7:6 were 3. This describes sensor input, not ISP output layout.
 * Y 16..235, chroma 16..240. Treating that as full-range 4:2:0 is what made the
 * captured picture both displaced and washed out.             VERIFIED by decode
 */
void ax32xx_csiTypeSet(u8 type)
{
    u32 v = REG(0xb000);
    REG(0xb000) = v & ~CSI_MODE_FORMAT;
    v = REG(0xb000);
    REG(0xb000) = v & ~CSI_MODE_DATA_BW;
    v = REG(0xb000);
    u32 field = (type < 3) ? (type << 10) : (((type - 1) & 3) << 6);
    REG(0xb000) = field | v;
}

/* 0x0203466c  ax32xx_csiModeSet(mode, en): plain set/clear of mode bits. */
void ax32xx_csiModeSet(u32 mode, u8 en)
{
    u32 v = REG(0xb000);
    REG(0xb000) = en ? (mode | v) : ((~mode) & v);
}

/* 0x02034ee4  ax32xx_csiEnable(en)
 * Bit 31 of b014 is the interrupt enable, bit 0 of b000 the block enable. */
void ax32xx_csiEnable(u8 en)
{
    if (en == 0) {
        REG(0xb014) &= ~0x80000000u;
        REG(0xb000) &= ~1u;
    } else {
        REG(0xb014) |= 0x80000000u;
        REG(0xb000) |= 1u;
    }
    ax32xx_intEnable(0xe, en != 0);
}

/* 0x020348ec  ax32xx_csiLCDFrameSet(yaddr, uvaddr)
 * The Y and UV planes are armed by exactly these two register writes -- nothing
 * else. A zero argument leaves that plane unchanged. */
void ax32xx_csiLCDFrameSet(u32 yaddr, u32 uvaddr)
{
    if (yaddr)  { REG(0xb060) = yaddr;  }
    if (uvaddr) { REG(0xb064) = uvaddr; }
}

/* 0x020347ec  ax32xx_csiSizeSet(width, height)
 * b01c holds the SENSOR input size, not the output geometry: the measured
 * 0x02cf04ff decodes to 1280x720. */
void ax32xx_csiSizeSet(u16 width, u16 height)
{
    REG(0xb01c) = (u32)height * 0x10000 - 0x10000 | (u32)width - 1;
}

/* 0x02034420  ax32xx_csiInit()
 * csiIOConfig() plus the module clock. The second line clears bit 27 of 0x8840
 * (SYS_CLK_DVP_CSI) because ax32xx_sysClkSet sets the bit when its argument is 0.
 * sensor_init never calls this, and neither did Luma at first, so the capture
 * block ran unclocked: frames arrived banded and the chroma plane was never
 * written. Clearing the gate fixed the missing chroma.         VERIFIED
 */
void ax32xx_csiInit(void)
{
    ax32xx_csiIOConfig();
    ax32xx_sysClkSet(0x1b, 1);
}

/* 0x02034918  ax32xx_csiLCDDmaEnable(en)
 * Arming is more than one bit: it also programs b00c and the interrupt mask. */
void ax32xx_csiLCDDmaEnable(u8 en)
{
    if (en == 0) {
        REG(0xb018) &= ~0x40000u;
        REG(0xb014) &= ~0x00080042u; /* exact stock mask: preserve global enable */
        REG(0xb00c) = 0x8005au;
    } else {
        REG(0xb00c) = 0x5au;
        REG(0xb014) |= 0x8008005au;
        REG(0xb018) |= 0x40000u;
    }
}

/* 0x020349b4  ax32xx_csiLCDDmaKick()
 * This IS the normal per-frame kick. hal_lcdSwitchFrame2 restores destination
 * addresses first, then calls this. The ISR's clear/reset/set is error recovery. */
void ax32xx_csiLCDDmaKick(void)
{
    if (REG(0xb018) & 0x200000u) { REG(0xb018) |= 0x40000u; }
}

/* 0x020349f8  ax32xx_csiLCDScaler(src_w,src_h,tar_w,tar_h,sx,sy,ex,ey,stride,*a)
 *
 * Geometry, all of which our measured values decode to exactly:
 *   b030 = (ey << 16) | sy        measured 0x05000000 -> x 0..1280
 *   b034 = (ex << 16) | sx        measured 0x02d00000 -> y 0..720
 *   b180 = (tar_h << 16) | tar_w  measured 0x00b40140 -> 320x180
 *   b184/b188 = the scaling ratios, b068 = stride
 * then a kick through b008 (LS_KS latch). */
void ax32xx_csiLCDScaler(u16 src_w, u16 src_h, u16 tar_w, u16 tar_h,
                         u16 sx, u16 sy, u16 ex, u16 ey, u16 stride,
                         u32 *anti_lsawtooth)
{
    if ((int)tar_w <= (int)(ex - sx) && (int)tar_h <= (int)(ey - sy)) {
        REG(0xb030) = ((u32)ex << 16) | sy;
        REG(0xb034) = ((u32)ey << 16) | sx;
        ax32xx_ispModeSet(0x20000, 1);
        REG(0xb180) = ((u32)tar_h << 16) | tar_w;
        REG(0xb184) = ((u32)(ex - sx) * 0x10000) / (u32)(tar_w >> 1);
        REG(0xb188) = ((u32)(ey - sy) * 0x10000) / (u32)(tar_h >> 1);
        if (g_lsawtooth_loaded == 0) {
            for (int i = 0; i < 0x18; ++i) { REG(0xb18c + 4*i) = anti_lsawtooth[i]; }
            g_lsawtooth_loaded = 1;
        }
        REG(0xb068) = stride;
        ax32xx_ispModeSet(0x400000, 1);
        REG(0xb008) |= 0x80000u;
    }
}

/* ---------------------------------------------------------------------------
 * 0x0203404c  ax32xx_csiIRQHandler()
 *
 * THE function Luma was missing. Run per frame-event, it:
 *   1. toggles b000 bits 27:26 on event 0x10
 *   2. dispatches up to ten registered per-event callbacks, clearing each bit
 *   3. handles event 0x8000 with a fixed clear of 0xc000
 *   4. re-arms the LCD transfer as CLEAR -> pip reset -> SET
 *   5. ends with `b00c = events`, clearing everything it handled
 * and it carries a watchdog: if more than 100 events accumulate unserviced it
 * hangs in a bare infinite loop. That is exactly what a pipeline with nobody
 * servicing it does.
 *
 * Luma's csi_service() originally did only step 4's final SET, with no clear and
 * no pip reset -- so a completed transfer was never restarted, and capture was
 * intermittent rather than absent.                                    VERIFIED
 * ------------------------------------------------------------------------ */
void ax32xx_csiIRQHandler(void)
{
    static unsigned timeout_counter;
    u32 events = REG(0xb00c);

    if (events & 0x10u) {
        REG(0xb000) = (REG(0xb000) & 0xf3ffffffu) | 0x04000000u;
    }
    if (events & 0x8u) {
        g_frame_count++;
    }

    for (int i = 0; i < 10; ++i) {
        if ((1u << i) & events) {
            REG(0xb00c) = 1u << i;              /* clear just this one */
            if (g_event_cb[i]) { g_event_cb[i](); }
            timeout_counter = 0;
        }
    }
    if (events & 0x8000u) {
        REG(0xb00c) = 0xc000u;
        if (g_event_cb_hi) { g_event_cb_hi(); }
        timeout_counter = 0;
    }
    if (events & 0xe0970u) {
        if (100 < ++timeout_counter) { for (;;) { } }     /* watchdog */
        if ((events & 0x10u) && (REG(0xb018) & 0x40000u)) {
            REG(0xb018) &= ~0x40000u;
            ax32xx_pipCounterReset(1);
            REG(0xb018) |= 0x40000u;
        }
    }
    REG(0xb00c) = events;
}


/* ---------------------------------------------------------------------------
 * REGISTER NAMES -- see isp_csi_registers.md
 *
 * The whole 0xb000-0xb1E8 block is the SDK's SPRGROUP_SFR6 group, and
 * ax32xx/inc/ax329x.h names all 124 of its registers. Use those names.
 *
 *   b000 CSIMODE            b004 ISPMODE           b008 ISPCTRL
 *   b00c CSI_INTR           b014 CSI_INTEN         b018 CSI_DMA_MODE
 *   b01c CSI_IMAGE_SIZE     b030 CSI_I2L_HCROP     b034 CSI_I2L_VCROP
 *   b060 CSI_I2L_YBADR      b064 CSI_I2L_UVBADR    b068 CSI_I2L_STRD
 *   b180 ISP_LS_SIZE        b184 ISP_LS_HSTP       b188 ISP_LS_VSTP
 *   b18c ISP_LS_HTAB0..     b1cc ISP_LS_VTAB0..
 *
 * "LS" is the LINE SCALER, not lens shading, so b180/b184/b188 really are the
 * scaler target and steps and b18c+ really is the anti-lsawtooth table -- as the
 * text below says. That is worth stating because the "LS = lens shading" reading
 * is plausible enough that it nearly caused this file to be "corrected" into being
 * wrong.
 *
 * Two bit names matter more than the rest:
 *   b018 bit 0x40000  = CSI_OUTPUT_LCDEN    -- the bit called "arm" here
 *   b018 bit 0x200000 = CSI_OUTPUT_LDMAMANEN-- manual mode, why a kick is needed
 *   b008 bit 19       = ISP_CTRL_LS_KS      -- the bit called "latch" here
 *   b00c bit 1        = CSI_IRQ_LCD_FRAME_END  -- the correct per-frame trigger
 *   b00c bit 4        = CSI_IRQ_SEN_STATE_ERR  -- what the stock guard uses
 *
 * The last two are the reason the stock re-arm condition is misleading: the
 * vendor re-arms on SENSOR STATE ERROR as error recovery, not on frame end. See
 * isp_csi_registers.md for the full argument and the measurements.
 * ------------------------------------------------------------------------ */

/* ===========================================================================
 * GAPS BETWEEN LUMA'S CSI SETUP AND THE SDK'S OWN SCALER RECIPE
 *
 * Found by diffing what Luma writes against what the vendor writes for the same
 * job. ax32xx_csiLCDScaler (0x020349f8, transcribed above) is the encoder for the
 * scaler register set. It writes SEVEN things:
 *
 *     b030  crop x window
 *     b034  crop y window
 *     ISPMODE(0xb004) |= 0x20000   LCDCROP enable        <-- Luma did not write
 *     b180  target size
 *     b184  horizontal ratio     <-- Luma did not write
 *     b188  vertical ratio       <-- Luma did not write
 *     b18c..b1c4  anti-lsawtooth table (24 words)   <-- Luma did not write
 *     b068  stride
 *     ISPMODE(0xb004) |= 0x400000  LSEN enable           <-- Luma did not write
 *     b008  |= 0x80000 latch
 *
 * Luma writes every one of those EXCEPT b184, b188 and the table. That is not a
 * style difference: the ratio registers are what tell the scaler how fast to step
 * through the 1280x720 source to land on a 320x180 target. For the measured
 * geometry the vendor formula gives
 *
 *     b184 = (ex - sx) * 0x10000 / (target_w >> 1) = 1280 * 65536 / 160 = 0x80000
 *     b188 = (ey - sy) * 0x10000 / (target_h >> 1) =  720 * 65536 /  90 = 0x80000
 *
 * A scaler left at stale or default ratios produces distorted, sheared output --
 * which is what "garbled chunks" looks like. TEST: write b184 = b188 = 0x80000
 * and re-capture. The anti-lsawtooth table is a separate, second-order cause
 * (aliasing, not geometry); its 24 words are loaded once behind a loaded-flag and
 * have NOT yet been extracted from the image.
 *
 * THE ONE THAT MAY MATTER MOST IS ISPMODE. The scaler's crop path has to be
 * ENABLED, not just configured, and the enable lives in a different register from
 * everything else here: ISPMODE at 0xb004 (SPRGROUP_SFR6 + 4, i.e. 0x8000 +
 * 6*0x800 + 4 -- it is not in the b0xx lane you would expect, it just happens to
 * land there). The SDK turns on ISP_MODE_LCDCROP (0x20000) before writing the
 * target size and ISP_MODE_LSEN (0x400000) before the latch. Luma previously
 * touched neither, so the scaler was configured with its own output path off.
 * Both are read-modify-write ORs, so nothing else in ISPMODE is disturbed.
 *
 * All four omissions (LCDCROP, b184, b188, LSEN) were added to Luma on
 * 2026-10-16 and verified present in the built image by disassembly. Whether they
 * are the cause of the garbled frames is NOT yet known -- they are a faithful
 * match to the vendor recipe, which is a different claim from "verified on
 * hardware". That distinction is the whole point of this file.
 *
 * CAUTION ON THE FIXTURE: tests/fixtures/hardware/original-2026-10-15.json records
 * 13 CSI registers from the stock preview, and b184/b188 are NOT among them. So
 * their stock values are unknown, and "Luma matches the measured contract" was
 * only ever true for the 13 registers that were captured.
 *
 * ---------------------------------------------------------------------------
 * THE LIKELIER CAUSE OF THE INTERMITTENCY: THE STOCK ISR MAY STILL BE RUNNING
 *
 * Luma writes b014 = 0x800ef87b -- the measured stock value, verbatim. Bit 31 of
 * b014 is the CSI interrupt enable. So Luma turns the CSI interrupt ON, and it
 * never masks it again.
 *
 * AND THE STOCK DOES NOT DO THAT HERE. The fixture already had the answer, under
 * display_contract.csi_interrupt_reg_b014_differs_by_mode:
 *
 *     USB mode      b014 = 0x0000f818     bit 31 CLEAR -- CSI interrupt OFF
 *     preview mode  b014 = 0x800ef87b     bit 31 SET   -- CSI interrupt ON
 *
 * In preview mode the stock runs capture with the interrupt enabled and its own
 * ISR doing the servicing. Luma copied the preview register set -- including the
 * interrupt enable -- but is NOT in the stock preview state and has no registered
 * callbacks backing it. So Luma has switched on a servicing path that is wired to
 * somebody else's bookkeeping. Clearing bit 31 gives 0x000ef87b, the same mask
 * with interrupts off, which is the right shape for a polling application.
 *
 * In a bounded RAM run the stock CSI interrupt vector is still installed: Luma
 * does not replace it. If the controller-level enable for IRQ 0x0e is also still
 * set (stock init did that), then ax32xx_csiIRQHandler runs on every CSI event
 * *in addition to* Luma's polling loop in csi_service():
 *
 *   - the stock handler dispatches its own registered callbacks, which service
 *     the STOCK preview planes (b060/b064 = 0x02104080/0x02112180), not Luma's
 *   - it re-arms the LCD transfer (clear 0x40000, pip reset, set 0x40000)
 *   - Luma's csi_service re-arms the same transfer from the polling side
 *
 * Two writers, one register set, and one of them pointed at somebody else's
 * buffer. That fits all three observed capture states: both planes written (Luma's
 * buffers won), Y only (a partial), neither (the stock's buffers won and Luma read
 * its own untouched memory).
 *
 * It also explains the hangs that need a pinhole reset. The stock handler carries
 * the watchdog noted above:
 *
 *     if (events & 0xe0970) { if (100 < ++timeout_counter) { for (;;) { } } ... }
 *
 * Its counter is reset only when a low event bit dispatches a callback. If events
 * keep arriving with those bits set and the re-arm condition is not met, the
 * count runs past 100 and the CPU spins forever in a bare loop. From outside that
 * is a dead camera with the backlight on, which is exactly what was observed
 * before a power cycle was needed.
 *
 * TEST (cheap, reversible, and independent of the ratios above): clear bit 31 of
 * b014 so the CSI interrupt cannot fire, leave Luma's polling csi_service() as the
 * only servicing path, and re-capture. If the three states collapse to one and
 * the resets stop, this was it.
 * ======================================================================== */

/* ===========================================================================
 * RESOLVED 2026-10-16: the frozen viewfinder was an unsatisfiable re-arm guard
 *
 * Symptom, reported from the panel: Luma's viewfinder showed a static, never
 * updating field of grey dither -- "patterns and bars" -- that did not resemble
 * the scene and did not change when the camera was panned.
 *
 * ROOT CAUSE. The transfer is restarted by writing b018 bit 0x40000. That bit
 * SELF-CLEARS when a transfer completes. The stock handler guards the restart
 * with `b018 & 0x40000` because it runs FROM THE INTERRUPT -- i.e. while the
 * transfer is still in flight, when the bit is still set. Luma services the CSI
 * by POLLING, and a poller only ever observes the event after completion, when
 * the bit is already clear. The guard could therefore never pass and the capture
 * ran exactly once.
 *
 * Two failed attempts, both measured on hardware:
 *   - keeping the stock's `b018 & 0x40000` guard:  rearms = 0 over 45 polls
 *   - gating on `events & 0x10`:  the event set is NOT stable. It measured 0x1A
 *     (bit 4 present) in one run and 0xC00A (bit 4 ABSENT) in another, so the
 *     re-arm silently did nothing whenever 0x10 was missing. This is the version
 *     that was on the panel when the user reported "nothing moves".
 *
 * WORKING FORM: re-arm whenever the previous transfer has finished, with no
 * dependence on the event set --
 *
 *     if (!(REG(0xb018) & 0x40000u)) {
 *         REG(0xb018) &= ~0x40000u;
 *         pip = REG(0x8980);
 *         if (1u == (((pip >> 1) & 1u) ^ 1u)) { REG(0x8980) = pip | 4u; }
 *         REG(0xb018) |= 0x40000u;
 *     }
 *
 * Measured after the fix: rearms = 59 over 117 polls, Y plane 57488/57600 bytes
 * carrying real data and the UV plane 28712/28800 -- the chroma plane had been
 * completely flat at 128 before. The scene is live and updates under panning.
 *
 * Also note the early return in the service routine had to go: returning when
 * b00c is zero skips the re-arm, which recreates the freeze the moment events
 * stop arriving.
 *
 * ---------------------------------------------------------------------------
 * TWO DEAD ENDS, so nobody re-tries them
 *
 * 1. Overriding the CSI register block with the 13 captured stock values BLANKS
 *    capture entirely (Y and UV both read as pure zeros, byte-identical across
 *    runs). The vendor's own hal_csiRegister, called from sensor_init, configures
 *    81 of 128 registers including the ISP/dither/gamma tables. Let it run last
 *    and keep the final say. The full working block is in csi-registers.md.
 *
 * 2. Clearing b014 bit 31 STOPS EVENT GENERATION: measured events_seen went from
 *    0x0000C00A down to 0x00000000 across 4,000,000 polls. The stock reads
 *    b014 = 0x0000F818 with bit 31 clear in USB mode, but that is a mode where
 *    the CSI is idle -- not a configuration to copy while capturing.
 *
 * Also measured and correct: b184/b188 (the scaler ratios) are ZERO in a working
 * capture, so the earlier hypothesis that omitting them caused shearing was
 * wrong. And csiIOConfig, added on its own, made capture worse (plane untouched).
 * ======================================================================== */

/* ===========================================================================
 * FOUND 2026-10-16: hal_lcdPreviewStart -- the CALLER we never looked at
 *
 * We had transcribed every CALLEE in this file (csiLCDScaler, csiLCDDmaEnable,
 * csiSizeSet, csiLCDFrameSet, csiIRQHandler, csiInit) and still could not answer
 * "what geometry does the stock use, and how does it avoid tearing". The answer
 * was one `references` query away, in the function that CALLS them with real
 * arguments. Lesson: decompiling the leaf functions tells you the mechanics, not
 * the values. Always go up one level to the caller.
 *
 *   0x02039f9c  hal_lcdPreviewStart(u16 width, u16 height)     [SDK: hal_lcd.h]
 *   0x02039dc8  hal_lcdPreviewStop(void)
 *
 * Decompiled body, in order:
 *
 *   DAT_ram_020ce97c = hal_lcdGetIdleBuffer();          // a BUFFER, not an address
 *   hal_dispframeVideoCfg(DAT_ram_020ce97c,
 *       *(u16 *)(DAT_ram_020ce980 + 0x38),              // width
 *       *(u16 *)(DAT_ram_020ce980 + 0x3a),              // height
 *       *(u16 *)(DAT_ram_020ce980 + 0x3c),
 *       *(u16 *)(DAT_ram_020ce980 + 0x3e));
 *   ax32xx_csiLCDScaler(width, height,
 *       *(u16 *)(*(int *)(DAT_ram_020ce968 + 0xc) + 10),  // target width
 *       *(u16 *)(*(int *)(DAT_ram_020ce968 + 0xc) +  8),  // target height
 *       DAT_ram_020ce984, DAT_ram_020ce988,               // sx, sy
 *       DAT_ram_020ce986, DAT_ram_020ce98a,               // ex, ey
 *       *(u16 *)(puVar1 + 0x28),                          // STRIDE from the buffer
 *       (uint *)(DAT_ram_020ce980 + 0xd0));               // anti-lsawtooth table
 *   ax32xx_csiOutputSet(0x20000, 0);                     // clear CSI_OUTPUT_RELOAD
 *   ax32xx_csiOutputSet(0x200000, 1);                    // set CSI_OUTPUT_LDMAMANEN
 *   ax32xx_csiLCDFrameSet(*(uint *)(puVar1 + 4),         // Y address, from the buffer
 *                         *(uint *)(puVar1 + 8));        // UV address, from the buffer
 *   iVar2 = 0x100000;
 *   do { iVar3 = ax32xx_csiScalerCheck(); if (iVar3 != 0) break; } while (--iVar2);
 *   ax32xx_csiLCDDmaEnable(1);
 *
 * THREE THINGS THIS REVEALS THAT WERE NOT KNOWN BEFORE
 *
 * 1. DOUBLE BUFFERING. The plane comes from hal_lcdGetIdleBuffer(), a frame
 *    buffer manager with idle/active buffers, and Y, UV and STRIDE all come out
 *    of that descriptor. The stock is not pointing the CSI at a fixed address.
 *    Luma points the CSI at one buffer and then reads that same memory in
 *    sensor_copy(), unsynchronised -- which is the vertically moving tear band
 *    the user sees on the panel: the imager writing while the reader reads.
 *
 * 2. A SYNC STEP WE SKIPPED. ax32xx_csiScalerCheck (0x02034e80) is
 *    (REG(0xb008) >> 19 ^ 1) & 1 -- it returns 1 once ISP_CTRL_LS_KS has
 *    SELF-CLEARED. The stock spins on that between csiLCDFrameSet and
 *    csiLCDDmaEnable(1), i.e. it does not arm the DMA until the line-scaler
 *    kickstart has finished. Luma armed it in the next instruction. Added.
 *
 * 3. THE EVENT MASK. ax32xx_csiLCDDmaEnable(1) also writes b00c = 0x5a, which is
 *    CSI_INTR's enable mask (bits 1,3,4,6 -- including bit 1,
 *    CSI_IRQ_LCD_FRAME_END, the correct re-arm trigger). Luma never set it and
 *    was relying on whatever the block held. Added.
 *
 * Those two additions are correct against the vendor code and are in, but they did
 * NOT change the geometry: measured after, rows 0-119 still flat at ~126 with
 * within-row stdev 2.6, and rows 120-179 still carrying the content. So the
 * picture occupying only the bottom third is NOT the missing sync.
 *
 * NEXT: the buffer manager is now the prime suspect for both remaining defects,
 * because the buffer descriptor carries the stride and the geometry, and its
 * idle/active swap is the double buffering. Read hal_lcdGetIdleBuffer and
 * hal_dispframeVideoCfg (and the struct at DAT_ram_020ce980, whose +0x38..+0x3e
 * are width/height and +0xd0 the scaler table) rather than sweeping registers.
 * ======================================================================== */

/* ===========================================================================
 * FOUND 2026-10-16: the frame-buffer descriptor (hal_dispframeVideoCfg)
 *
 *   0x02044ce0  hal_dispframeVideoCfg(disp_frame_t *f, u16 posX, u16 posY,
 *                                     u16 width, u16 height)
 *   0x02044f64  hal_dispframeMalloc(u8 frame_type) -> disp_frame_t *
 *   0x02039f5c  hal_lcdGetIdleBuffer() = hal_dispframeMalloc(DAT_ram_020ce961 == 0)
 *   0x02044ff8  hal_dispframeFree(disp_frame_t *)
 *
 * disp_frame_t layout, from the non-rotated branch of hal_dispframeVideoCfg:
 *
 *   +0x04  Y plane address
 *   +0x08  UV plane address   = Y + stride * height
 *   +0x0c  rotated Y address
 *   +0x10  rotated UV address
 *   +0x18  total size         = stride * height * 3 / 2
 *   +0x20  width     +0x22  height
 *   +0x24  posX      +0x26  posY
 *   +0x28  STRIDE             = (width + 0x1F) & ~0x1F
 *   +0x2a  rotation   +0x2e  type
 *
 * THE STRIDE RULE, confirmed twice over: stride is the width rounded UP to a
 * 32-byte multiple. For a 320-wide plane that is 320 exactly, so Luma's CSI
 * stride is right. For the stock's 240-wide DE plane it is 256 -- which is the
 * long-standing puzzle of why the panel pitch is 256 and not 240. It is not a
 * special case; it is this formula.
 *
 * DOUBLE BUFFERING. hal_lcdGetIdleBuffer picks from one of TWO free lists
 * (hal_dispframeMalloc indexes them by frame_type, `+ frame_type * 0x98`), and
 * the type comes from DAT_ram_020ce961. So the vendor has two frame buffers and
 * alternates. Luma points the CSI at a single buffer and then READS THAT SAME
 * MEMORY in sensor_copy(), unsynchronised -- which is the vertically moving tear
 * band seen on the panel: the imager writing while the reader reads. That is the
 * next thing to fix, and it is also the leading suspect for the remaining
 * geometry defect, because a read racing the writer can straddle two frames.
 *
 * Also note the video WINDOW is positioned: hal_dispframeVideoCfg takes posX/posY
 * from DAT_ram_020ce980 + 0x38/+0x3a, and width/height from +0x3c/+0x3e. The
 * fallback stock preview window is 180 wide at x=30 on the 240-wide panel, with
 * stride 192 = round_up(180,32).
 * ======================================================================== */
