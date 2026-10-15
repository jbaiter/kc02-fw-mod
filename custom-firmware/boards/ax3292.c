/* SPDX-License-Identifier: MIT
 * QEMU bring-up HAL. Register contracts: vendor AX3292 SDK ax329x.h and
 * docs/BOARD_RE.md. This is NOT a silicon-ready cold boot or printer driver.
 * Missing: clocks/SDRAM tuning, pinmux, panel timing/commands, sensor tables,
 * USB controller and exception/interrupt entry. Do not flash this RAM image.
 */
#include "luma.h"
#include "storage.h"
#include "input.h"
#include "sd_hw.h"
#include "ax_time.h"
#include "ax_sensor.h"
#include "ax_cam_power.h"
#include "ax_iic_soft.h"
#include "h63p_init.h"
#ifndef BOARD_ID
#error BOARD_ID must be 0 (original), 1 (x8d) or 2 (z12)
#endif
#define REG(n) (*(volatile __sfr u32 *)(n))
static u16 frame[PIXELS], camera[PIXELS], photo[PIXELS];
static u16 theme_bg[PIXELS], theme_boot[PIXELS];
static u8 bitmap[54+PIXELS*3];
/* Panel geometry, measured on the original KC02 in stock USB mode: the DE video
 * plane is 240x320 with a 256-byte pitch (stride register 0x40 words), not the
 * 240-byte pitch a straight 240-wide plane would suggest. Using 240 was never
 * observed working on hardware. */
#define SENSOR_COLS 320u   /* imager output width, measured */
#define SENSOR_ROWS 180u   /* imager output height, measured */
#define LCD_PITCH 256u
#define LCD_ROWS  320u
#define LCD_UVROWS (LCD_ROWS/2u)
static u8 lcd_y[LCD_ROWS*LCD_PITCH] __attribute__((aligned(32)));
static u8 lcd_uv[LCD_UVROWS*LCD_PITCH] __attribute__((aligned(32)));
static u8 sensor_y[PIXELS] __attribute__((aligned(32)));
static u8 sensor_uv[PIXELS/2] __attribute__((aligned(32)));
static u8 dma[512] __attribute__((aligned(32)));
volatile u32 luma_clock_delta;
static SdHw sd;
static Fat fs;
static unsigned photo_ids[GALLERY_MAX], count;
/* GDB observation points; application state is never supplied by the host. */
volatile App luma_app;
volatile u32 luma_frames;

void *memcpy(void *dest, const void *src, size_t n)
{
    u8 *d=dest; const u8 *s=src;
    while (n--) { *d++=*s++; }
    return dest;
}
void *memset(void *dest, int v, size_t n)
{
    u8 *d=dest;
    while (n--) { *d++=(u8)v; }
    return dest;
}
static int read_sector(u32 lba, u8 *out) { return sd_hw_read(&sd, lba, out); }
static int write_sector(u32 lba, const u8 *in) { return sd_hw_write(&sd, lba, in); }
static int read_file(const char *name, u8 *out, unsigned cap, unsigned *n)
{
    return fat_read(&fs,name,out,cap,n);
}
static unsigned photo_count(void)
{
    count=fat_photos(&fs,photo_ids,GALLERY_MAX);
    return count;
}
static int load_photo(unsigned index, u16 *out)
{
    char name[13]; unsigned n;
    if (index>=count) { return 0; }
    photo_name(name,photo_ids[index]);
    return read_file(name,bitmap,sizeof(bitmap),&n) && bmp_read(bitmap,n,out);
}
static int save_photo(const u16 *pixels)
{
    char name[13];
    unsigned id=1, i, n;
    photo_count();
    if (count==GALLERY_MAX) { return 0; }
    for (i=0; i<count; i++) { if (id<=photo_ids[i]) { id=photo_ids[i]+1; } }
    if (id>99999) { return 0; }
    photo_name(name,id);
    n=bmp_write(pixels,bitmap,sizeof(bitmap));
    return n && fat_create(&fs,name,bitmap,n);
}
static void lens(int selfie)
{
    if (BOARD_ID==2) {
        if (selfie) { REG(0x8010)&=~0x800u; } else { REG(0x8010)|=0x800; }
    }
}
static int unavailable(void) { return 0; }
/* Intentionally no GPIO strobe backend until temperature/supply interlocks
 * and actual head energy limits have been validated on each board. */
static const Platform platform={read_file,save_photo,load_photo,photo_count,
                                0,unavailable,unavailable,BOARD_ID==2 ? lens : 0};
static u8 clip(int v) { return v<0 ? 0 : v>255 ? 255 : v; }
/* Set from the packet: draw colour bars instead of the imager, so the display
 * path can be judged on its own. If bars arrive in colour, the rendering and DE
 * path are sound and any defect is in the capture format; if they arrive grey,
 * the chroma is being lost between present() and the panel. */
volatile u32 luma_test_pattern;

static void sensor_copy(void)
{
    unsigned x,y;
    if (luma_test_pattern) {
        for (y=0; y<LH; y++) {
            for (x=0; x<LW; x++) {
                unsigned bar = x * 8u / LW;
                camera[y*LW+x] = (bar & 1u) ? 0xF800u : ((bar & 2u) ? 0x07E0u : 0x001Fu);
            }
        }
        return;
    }
    for (y=0; y<LH; y++) {
        for (x=0; x<LW; x++) {
            /* The CSI input format is CSI_TYPE_YUVR422 (type 4, decoded from
             * b000 bits 7:6 against the SDK enum): 4:2:2 in VIDEO range, i.e.
             * Y 16..235 and chroma 16..240. Treating those as full range both
             * crushes contrast and throws away most of the saturation, which is
             * what made the picture look nearly monochrome. Expand to 0..255
             * before the matrix.
             *
             * The imager gives 320x180; the panel view is 320x240, so the rows
             * past the capture are letterboxed with a neutral fill. */
            int l = y < SENSOR_ROWS ? sensor_y[y*LW+x] : 16;
            unsigned uvrow = y/2;
            int u = (y < SENSOR_ROWS ? sensor_uv[uvrow*LW+(x&~1u)] : 128) - 128;
            int v = (y < SENSOR_ROWS ? sensor_uv[uvrow*LW+(x&~1u)+1] : 128) - 128;
            l = (l - 16) * 255 / 219;
            u = u * 255 / 224;
            v = v * 255 / 224;
            if (l < 0) { l = 0; } else if (l > 255) { l = 255; }
            if (u < -128) { u = -128; } else if (u > 127) { u = 127; }
            if (v < -128) { v = -128; } else if (v > 127) { v = 127; }
            int r=clip(l+((359*v)>>8)), g=clip(l-((88*u+183*v)>>8)), b=clip(l+((454*u)>>8));
            camera[y*LW+x]=RGB(r,g,b);
        }
    }
}
/* Service the LCD frame events and kick the next frame.
 *
 * The stock firmware does this from its LCD interrupt at 0x0203584c: every
 * LCDSTA(0xa804) event bit is acknowledged by writing that bit eight positions
 * higher, and the frame event additionally calls ax32xx_lcdKick(), which is
 * LCD_CON(0xa800) bit 1. A polling application has to do the same work, because
 * without the acknowledgement the controller stops producing frame boundaries --
 * and a DE commit is frame-gated, so the panel then stays on whatever frame it
 * last latched. Observed on hardware as Luma sitting on its first frame, the boot
 * screen, with the progress bar never advancing while the code was demonstrably
 * running. */
static void lcd_service(void)
{
    u32 sta;
    unsigned bit;
    for (;;) {
        sta = REG(0xa804) & 0xffu;      /* event bits live in the low byte */
        if (!sta) { break; }
        for (bit=0; bit<8; bit++) {
            if (sta & (1u<<bit)) { REG(0xa804) = 1u << (bit+8); }
        }
    }
    REG(0xa800)|=2;                     /* frame kick */
}

/* Service CSI frame events. B00C is a write-one-to-clear event register; the
 * stock firmware clears it from its CSI interrupt and re-arms the capture path.
 * Luma has no ISR, so without this the pipeline delivers one frame and stops,
 * which would leave the viewport frozen on the first capture. */
/* CSI servicing telemetry. The re-arm guard below requires event 0x10 AND the
 * arm bit still set -- but the arm bit SELF-CLEARS when a transfer completes, so
 * a finished transfer can never be restarted by that path. These counters say
 * which events actually arrive, which is the fact we have been guessing at. */
volatile u32 luma_csi_seen, luma_csi_calls, luma_csi_rearms, luma_csi_rearm_armed;
static void csi_service(void)
{
    u32 events = REG(0xb00c);
    /* Deliberately no early return: the re-arm below must run every pass, even
     * when no event is pending. Returning here would recreate the frozen-frame
     * bug the moment events stop arriving. */
    if (events) {
        luma_csi_seen |= events;
        luma_csi_calls++;
    }

    /* Per-frame mode toggle, from the stock handler: on event 0x10 it clears
     * bits 27:26 of b000 and sets bit 26. */
    if (events & 0x10u) {
        REG(0xb000) = (REG(0xb000) & 0xf3ffffffu) | 0x04000000u;
    }

    /* Re-arm the LCD transfer exactly as ax32xx_csiIRQHandler does. Luma used to
     * only set the bit, which does not restart a completed transfer: the stock
     * sequence is clear, reset the pip counter, then set.
     *   b018 &= ~0x40000;  ax32xx_pipCounterReset(1);  b018 |= 0x40000;
     * and ax32xx_pipCounterReset (0x020329d4) is, decompiled:
     *   v = REG(0x8980); if (1 == (((v>>1)&1)^1)) REG(0x8980) = v | 4;
     */
    /* Re-arm on the frame event, WITHOUT requiring the arm bit to still be set.
     *
     * The stock handler guards this with `b018 & 0x40000` because it runs from the
     * interrupt, i.e. while the transfer is still in flight. A POLLING loop never
     * sees that moment: by the time b00c is read, the transfer has completed and
     * the arm bit has self-cleared, so the guard can never pass. The result was a
     * capture that ran exactly once and then froze -- a static, never-updating
     * viewfinder. Under takeover there is no fallback either, because Luma sets
     * PICMR=0, so the stock ISR that would normally do this is not running.
     *
     * luma_csi_rearm_armed records how often the bit was still set, so we can see
     * whether the old condition ever held at all. */
    /* ALWAYS re-arm; do not wait for a specific event bit.
     *
     * The arm bit (b018 0x40000) self-clears when a transfer completes, so a
     * POLLING consumer must restart the transfer itself. Two conditions were tried
     * and both failed on hardware:
     *   - the stock's `b018 & 0x40000` guard: unsatisfiable after completion
     *     (measured rearms = 0 with the guard, over 45 polls)
     *   - gating on event 0x10: the event set is not stable. Measured 0x1A in one
     *     run (bit 4 present) and 0xC00A in another (bit 4 ABSENT), so the re-arm
     *     silently did nothing whenever 0x10 happened to be missing.
     * Unconditional is the only form that cannot be defeated by either. This is
     * what makes the viewfinder update instead of freezing on its first frame. */
    if (!(REG(0xb018) & 0x40000u)) {
        u32 pip;
        luma_csi_rearms++;
        REG(0xb018) &= ~0x40000u;
        pip = REG(0x8980);
        if (1u == (((pip >> 1) & 1u) ^ 1u)) { REG(0x8980) = pip | 4u; }
        REG(0xb018) |= 0x40000u;
    }

    /* Clear every event this pass handled, as the stock handler ends with
     * b00c = events. */
    if (events) { REG(0xb00c) = events; }
}

static void present(const u16 *f)
{
    unsigned x,y;
    for (y=0; y<320; y++) {
        for (x=0; x<240; x++) {
            u16 c=BOARD_ID==1 ? f[(239-x)*LW+y] : f[x*LW+319-y];
            int r=(c>>11)*255/31, g=((c>>5)&63)*255/63, b=(c&31)*255/31;
            lcd_y[y*LCD_PITCH+x]=(77*r+150*g+29*b)>>8;
            if (!(x&1) && !(y&1)) {
                lcd_uv[(y/2)*LCD_PITCH+x]=clip(128+((-43*r-85*g+128*b)>>8));
                lcd_uv[(y/2)*LCD_PITCH+x+1]=clip(128+((128*r-107*g-21*b)>>8));
            }
        }
    }
    REG(0xc800)=1|2;   /* request the buffer commit at the next frame boundary */
    /* Advance the panel. Measured on hardware: ax32xx_lcdKick() at 0x02035814 is
     * exactly `LCD_CON |= 2`, and the stock firmware raises it from its LCD frame
     * interrupt. Luma has no ISR and polls instead, so it must kick the frame
     * itself. Without this the LCD never advances and the panel keeps showing the
     * last buffer the previous firmware scanned out -- observed as a frozen stock
     * screen while Luma was demonstrably running. */
    lcd_service();
}
/* Raw keypad ladder sample.
 *
 * This follows the stock hal_adcGetChannel sequence exactly, transcribed from the
 * firmware rather than assumed:
 *
 *   ax32xx_adcEnable(ch,1)      REG(0x9170) = ch|0x10, AND ax32xx_sysClkSet(29,1)
 *   ax32xx_adcSetBaudrate(1e6)  REG(0x9174) = 144000000/(baud*2)-1 = 71
 *   ax32xx_adcConverStart()     REG(0x9170) |= 0x80
 *   ax32xx_adcRead()            wait while REG(0x9170)&0x40, then REG(0x9178)&0x3ff
 *
 * Two of those were missing and both are fatal to a read. The ADC module clock
 * (SYS_CLK_ADC = 29) is gated in 0x8840 and the stock enables it from
 * ax32xx_adcEnable; a CLEAR gate bit is the enabled state, because
 * ax32xx_sysClkSet sets the bit when its enable argument is zero. The baud
 * divider was never programmed either. With both missing the conversion never
 * completed, so every keypad read reported "no key" and the UI looked dead to
 * input while it was demonstrably rendering. */
#define AX_ADC_CLK_GATE   0x8840u
#define AX_ADC_CLK_BIT    29u        /* SYS_CLK_ADC */
#define AX_ADC_BAUD_DIV   71u        /* 144 MHz / (1 MHz * 2) - 1 */
#define AX_ADC_CH_KEYPAD  3u

static u32 key_sample(void)
{
    unsigned timeout=100000;
    REG(AX_ADC_CLK_GATE) &= ~(1u<<AX_ADC_CLK_BIT);
    REG(0x9174)=AX_ADC_BAUD_DIV;
    REG(0x9170)=0x10u|AX_ADC_CH_KEYPAD;   /* enable + channel */
    REG(0x9170)|=0x80u;                   /* start conversion */
    while ((REG(0x9170)&0x40u) && --timeout) { }
    if (!timeout) { return 0xffffffffu; }
    return REG(0x9178)&0x3ffu;
}

static Key key_read(void)
{
    /* Unique ladder levels. X8D has only previous/next, not four directions. */
    static const u16 values[3][9]={
        {657,510,252,776,386,12,1024,1024,1024},
        {411,511,411,511,788,14,252,132,1024},
        {620,775,328,238,512,415,512,133,704}
    };
    static const Key keys[9]={KEY_PREV,KEY_NEXT,KEY_PREV,KEY_NEXT,KEY_OK,KEY_BACK,KEY_OK,KEY_INK,KEY_LENS};
    unsigned i, sample, best=25, at=9;
    u32 raw=key_sample();
    if (raw==0xffffffffu) { return KEY_NONE; }
    sample=raw;
    if (sample>990) { return KEY_NONE; }
    for (i=0; i<9; i++) {
        unsigned v=values[BOARD_ID][i];
        unsigned d=sample>v ? sample-v : v-sample;
        if (v<1024 && d<best) { at=i; best=d; }
    }
    return at<9 ? keys[at] : KEY_NONE;
}
/* Imager bring-up, now with the power/MCLK sequencing that was missing.
 * The rails (LSEN/DVDD and HSEN/AVDD) and the CSI MCLK divider are programmed
 * from ax_cam_power.h, transcribed from the stock routines. The IIC0 baud rate
 * is the stock 200 kHz, giving divisor 143 at APB_CLK 144 MHz.
 * Still NOT done here: releasing the sensor reset line and the CSI/DVP receive
 * configuration, so a successful ID read means power and the bus are right, not
 * that capture works.
 * Stock references: sensorAutoCheck 0x02058170, H63P item 0x020E4BA0,
 * hal_iic08bitAddrReadData 0x02038818 (slave 0x40, id register 0x0b). */
/* volatile: these are read into the bench beacon after hardware_init(), and as
 * plain statics the compiler is free to hoist those volatile stores past the
 * inlined sensor code, which is what made the beacon report stale zeros. */
static volatile u8 sensor_id;
static volatile int sensor_id_ack;
static volatile int sensor_table_ok;

/* Read the imager identity over the SOFTWARE IIC bus.
 *
 * The stock path is bit-banged GPIO on PE13/PE15 with MAP = 0, not the hardware
 * IIC0 controller, which is why every controller-based attempt failed. See
 * ax_iic_soft.h for the transcription. */
static volatile u32 sensor_raw_ack;
static volatile u32 sensor_dbg_slave, sensor_dbg_reg, sensor_dbg_read, sensor_dbg_rx;

static int sensor_read_id(u8 *id)
{
    *id = 0;
    /* Read first, with no power or clock writes in front of it. The standalone
     * probe that succeeded did exactly this: its rails and MCLK were already
     * correct, so touching them first only risked disturbing a working state. */
    ax_soft_init();
    ax_soft_start();
    ax_soft_send_byte(AX_SENSOR_SLAVE_ADDR);
    sensor_raw_ack = (u32)ax_soft_wait_ack();
    ax_soft_stop();
    /* Step-by-step read so a failure names the stage instead of just failing. */
    sensor_dbg_slave = sensor_dbg_reg = sensor_dbg_read = sensor_dbg_rx = 0xEE;
    ax_soft_start();
    ax_soft_send_byte(AX_SENSOR_SLAVE_ADDR);
    sensor_dbg_slave = (u32)ax_soft_wait_ack();
    if (!sensor_dbg_slave) { ax_soft_stop(); return 0; }
    ax_soft_send_byte(AX_SENSOR_ID_REG);
    sensor_dbg_reg = (u32)ax_soft_wait_ack();
    if (!sensor_dbg_reg) { ax_soft_stop(); return 0; }
    ax_soft_stop();
    ax_soft_start();
    ax_soft_send_byte((ax_u8)(AX_SENSOR_SLAVE_ADDR | 1u));
    sensor_dbg_read = (u32)ax_soft_wait_ack();
    if (!sensor_dbg_read) { ax_soft_stop(); return 0; }
    sensor_dbg_rx = ax_soft_recv_byte();
    ax_soft_stop();
    *id = (u8)sensor_dbg_rx;
    sensor_id_ack = 1;
    return 1;
}

static volatile u32 sensor_tab_written;   /* entries accepted before a failure */
static volatile u32 sensor_tab_fail_idx;  /* index of the rejected entry, or -1 */
static volatile u32 sensor_tab_fail_reg;
static volatile u32 sensor_tab_fail_val;
static volatile u32 sensor_tab_fail_stage;/* 1 = address byte, 2 = register, 3 = value */

/* Push the extracted H63P init table, stopping at the 0xffff terminator.
 *
 * Instrumented per stage, because "the table write failed" is not actionable:
 * which register the sensor rejected, and at which byte, is. */
__attribute__((unused)) static int sensor_write_init_table(void)
{
    unsigned i, n = 0;
    sensor_tab_written = 0;
    sensor_tab_fail_idx = 0xFFFFFFFFu;
    for (i = 0; i + 1u < sizeof(h63p_init); i += 2u) {
        u8 reg = h63p_init[i], val = h63p_init[i + 1u];
        if (reg == 0xFFu && val == 0xFFu) { sensor_tab_written = n; return 1; }
        ax_soft_start();
        ax_soft_send_byte(AX_SENSOR_SLAVE_ADDR);
        if (!ax_soft_wait_ack()) {
            sensor_tab_fail_idx = n; sensor_tab_fail_reg = reg;
            sensor_tab_fail_val = val; sensor_tab_fail_stage = 1;
            ax_soft_stop(); sensor_tab_written = n; return 0;
        }
        ax_soft_send_byte(reg);
        if (!ax_soft_wait_ack()) {
            sensor_tab_fail_idx = n; sensor_tab_fail_reg = reg;
            sensor_tab_fail_val = val; sensor_tab_fail_stage = 2;
            ax_soft_stop(); sensor_tab_written = n; return 0;
        }
        ax_soft_send_byte(val);
        if (!ax_soft_wait_ack()) {
            sensor_tab_fail_idx = n; sensor_tab_fail_reg = reg;
            sensor_tab_fail_val = val; sensor_tab_fail_stage = 3;
            ax_soft_stop(); sensor_tab_written = n; return 0;
        }
        ax_soft_stop();
        ++n;
    }
    sensor_tab_written = n;
    return 1;
}

/* Stock sensor_init at 0x020583f0. During a RAM run the vendor routine is
 * intact in memory, so call it rather than replaying its steps: it raises the
 * rails, sets MCLK, initialises the IIC, autodetects, writes the 100-entry
 * table, calls hal_csiRegister and runs the frame-rate/exposure setup -- and it
 * populates the ops struct at 0x020e32a0 that the later helpers require. Luma's
 * own reimplementation leaves the imager registered but never streaming, which
 * is consistent with the banded viewport. */
typedef void (*stock_fn0)(void);
#define STOCK_SENSOR_INIT 0x020583f0u

static int sensor_bringup(void)
{
    u8 id = 0;
    ((stock_fn0)STOCK_SENSOR_INIT)();
    sensor_table_ok = 1;             /* written by the stock routine */
    /* Verify over our own bit-banged bus, which is the part that is Luma's. */
    ax_soft_init();
    if (sensor_read_id(&id)) {
        sensor_id = id;
    }
    return sensor_id_ack && id == AX_SENSOR_H63P_ID;
}

static void hardware_init(void)
{
    REG(0x4800)=0;
    REG(0x9800)=1; /* Enable the periodic peripheral-event model. */
    /* TT is disabled in the stock USB handoff, not a free-running us clock.
     * SDK sequence, measured on original KC02; other boards remain QEMU-only. */
    REG(0x9900)=0;
    REG(0x9904)=0xffffffffu;
    REG(0x9908)=0;
    REG(0x9900)=0x42;
    /* Read-modify-write, never assign. The boot chain already configured the LCD
     * and DE; a plain assignment clears fields Luma does not model. Measured
     * stock USB mode has LCD_CON = 0x31 and DE_DISPLAY_CTRL = 0x2ce52083, so
     * writing 1 to either silently turns the panel path off. */
    REG(0xa800)|=1;
    REG(0xc810)=((320-1)<<16)|(240-1);
    REG(0xc818)|=1; /* YUV video plane; preserve the rest of the stock value. */
    REG(0xc820)=((320-1)<<16)|(240-1);
    REG(0xc824)=0;
    REG(0xc828)=(u32)lcd_y; REG(0xc82c)=LCD_PITCH/4;
    REG(0xc830)=(u32)lcd_uv; REG(0xc834)=LCD_PITCH/4;
    /* Commit the video registers once. Measured on original KC02: a staged
     * c828 does not become the active address c87c until DE_FRAME_CTRL bit 1
     * is raised, and the change completes at a frame boundary (bit 1 then
     * self-clears). Without this the first framebuffer never takes effect.
     * This is the SDK's ax32xx_deUpdate. */
    REG(0xc800)=1|2;
    /* CSI output geometry, from the measured stock preview contract. The imager
     * delivers 320x180, not Luma's 320x240 panel shape: b180 is (rows<<16)|cols
     * and stock reads back 0x00b40140, i.e. 180 rows of 320. b068 is a byte
     * stride here, unlike the DE stride which is in words. */
    REG(0xb060)=(u32)sensor_y; REG(0xb064)=(u32)sensor_uv;
    REG(0xb068)=SENSOR_COLS;
    REG(0xb180)=(SENSOR_ROWS<<16)|SENSOR_COLS;
    /* Luma draws straight into its own DE plane, so every OTHER writer into the
     * display path has to be off. Measured on real hardware, the stock firmware
     * leaves the rotate engine enabled with its target on the DE video plane
     * (ROTATECON = 5 and ROTTGYADR == c828), and it leaves the CSI-to-LCD DMA
     * configured. Either one keeps writing into the plane Luma is displaying,
     * which shows up as a bright block rolling across the UI. */
    REG(0x898c)=0;   /* rotate engine off: Luma does not compose through it */
    /* Capture path, set to the measured stock preview values rather than guessed.
     * b000 is the CSI mode word (DVP/data width/polarity), b014 the interrupt
     * enable and mask, b01c the sensor size, b030/b034/b078 the scaler taps.
     *
     * REVERTED 2026-10-16. This block deliberately runs BEFORE sensor_bringup(),
     * and that order is load-bearing: sensor_bringup() calls the vendor
     * sensor_init, which calls hal_csiRegister and configures the capture path
     * properly, for the imager it actually detected. Luma's values here are a
     * reconstruction from 13 captured registers. Letting them win instead
     * produced a blank, event-free capture where the vendor's own configuration
     * had been delivering real frames. Do not move this block after
     * sensor_bringup() again without hardware evidence. */
    /* Enable the CSI module clock. ax32xx_csiInit (0x02034420) is csiIOConfig()
     * plus this gate.
     *
     * csiIOConfig was tried here additively on 2026-10-16 and made capture WORSE:
     * the plane went from carrying real Y data to being completely untouched, so
     * whatever it remaps conflicts with the vendor's own configuration. The gate
     * alone is kept. If a pin-mux problem is ever proven, revisit with evidence. */
    REG(0x8840) &= ~(1u << 27);
    REG(0xb000)=0x0440062du;
    REG(0xb014)=0x800ef87bu;
    REG(0xb018)=0x00240000u;
    REG(0xb01c)=0x02cf04ffu;
    REG(0xb030)=0x05000000u;
    REG(0xb034)=0x02d00000u;
    REG(0xb078)=0x02d00500u;
    REG(0xb008)=1u<<19;      /* latch the scaler configuration */
    REG(0xb000)=0x0440062du; /* enable after the latch */
    /* Prove the millisecond clock advances, here rather than in the main loop so
     * the bounded bench entry measures it too. If it does not advance, the render
     * gate can only ever draw one frame, which looks exactly like a dead UI. */
    {
        AxTime probe={0,0,0};
        u32 t0=ax_time_ms(&probe,REG(0x9908));
        for (volatile unsigned spin=0; spin<200000u; spin++) { }
        u32 t1=ax_time_ms(&probe,REG(0x9908));
        luma_clock_delta=t1-t0;
    }
    /* Zeroed chroma is not neutral. sensor_copy() and the DE both subtract 128,
     * so all-zero U/V becomes (-128,-128) and renders as saturated green. That is
     * the green rectangle in the viewport and the green garbage at the edge of
     * the plane: with a 256-byte pitch and 240 active columns the padding columns
     * are never written by present(), so they stay .bss zero. Fill every plane to
     * a neutral field at init, and the active pixels overwrite their part of it. */
    {
        unsigned k;
        for (k=0; k<sizeof(sensor_y); k++) { sensor_y[k]=0x10; }
        for (k=0; k<sizeof(sensor_uv); k++) { sensor_uv[k]=0x80; }
        for (k=0; k<sizeof(lcd_y); k++)    { lcd_y[k]=0x10; }
        for (k=0; k<sizeof(lcd_uv); k++)   { lcd_uv[k]=0x80; }
    }
    /* Bounded, read-only bus probe; captures the ID when the imager answers.
     * Absence is not fatal yet: the display path works without the sensor.
     *
     * ORDER MATTERS: this runs AFTER the CSI block above on purpose. The vendor
     * sensor_init inside it is what finally configures the capture path. */
    (void)sensor_bringup();

    /* Re-point ONLY the two plane addresses, and nothing else.
     *
     * The vendor sensor_init just ran and left the capture path fully configured
     * for the imager it detected -- but its hal_csiRegister pointed b060/b064 at
     * the STOCK's buffers. Overriding the whole register set (as attempted on
     * 2026-10-16) blanked the capture and, with b014 bit 31 cleared, stopped
     * events being generated at all: measured events_seen went 0x0000C00A ->
     * 0x00000000. The vendor's configuration is the working one; only the
     * destination is ours to choose. Two writes, nothing else. */
    REG(0xb060)=(u32)sensor_y; REG(0xb064)=(u32)sensor_uv;
    lens(0);
}
void firmware_main(void)
{
    App *a=(App *)&luma_app;
    Input input={0};
    AxTime clock={0,0,0};
    int have_bg=0, have_boot=0;
    u32 last=~0u, capacity;
    unsigned n;
    hardware_init();
    app_init(a,&platform,ax_time_ms(&clock,REG(0x9908)));
    /* Draw before touching storage. sd_hw_init can block on a card the host
ows,
     * and a first frame that only appears after storage succeeds is a first frame
     * that never appears when storage misbehaves. On hardware this was exactly the
     * failure: the RAM takeover hung in storage init and the panel stayed frozen
     * on the previous firmware's screen, while the bench entry that skipped
     * storage rendered correctly. */
    app_render(a,frame,camera,photo,0,0,0);
    present(frame);
    capacity=sd_hw_init(&sd,dma);
    if (capacity) { fat_mount(&fs,read_sector,write_sector,capacity); }
    if (a->theme.background[0] && read_file(a->theme.background,bitmap,sizeof(bitmap),&n)) { have_bg=bmp_read(bitmap,n,theme_bg); }
    if (a->theme.boot[0] && read_file(a->theme.boot,bitmap,sizeof(bitmap),&n)) { have_boot=bmp_read(bitmap,n,theme_boot); }
    int dirty=1;
    for (;;) {
        csi_service();
        /* Service the panel every iteration: acknowledge frame events and kick.
         * The stock firmware does this from its LCD interrupt; Luma polls, so it
         * must do the same work or the controller stops producing frame
         * boundaries and the panel freezes on its last latched frame. */
        lcd_service();
        u32 now=ax_time_ms(&clock,REG(0x9908));
        int held;
        Key key=input_poll(&input,key_read(),now,&held);
        if (key!=KEY_NONE) { app_event(a,&platform,key,held,now,camera,photo); dirty=1; }
        /* Render on input as well as on the tick, so a stall in the clock cannot
         * make the UI look dead. */
        if (!dirty && now-last < 40) { continue; }
        dirty=0;
        last=now;
        if (a->screen==CAMERA) { sensor_copy(); }
        app_render(a,frame,camera,photo,have_bg ? theme_bg : 0,have_boot ? theme_boot : 0,now);
        present(frame);
        luma_frames++;
        /* On-panel diagnostics. A takeover has no USB readback, so the panel is
         * the only channel: row 5 carries a marker that moves once per frame, and
         * row 8 a bar whose length is the raw keypad ladder. A frozen marker means
         * the loop or the display has stopped; a short bar means a button is down
         * (idle reads ~1023, a press ~12..780). */
        {
            unsigned mark = (unsigned)(luma_frames % 240u), i;
            for (i = 0; i < 240u; ++i) { lcd_y[5u*LCD_PITCH+i] = (i == mark) ? 255u : 0u; }
            u32 ks = key_sample();
            unsigned bar = (ks == 0xFFFFFFFFu) ? 0u : (unsigned)(ks / 4u);
            if (bar > 240u) { bar = 240u; }
            for (i = 0; i < 240u; ++i) { lcd_y[8u*LCD_PITCH+i] = (i < bar) ? 200u : 0u; }
        }
    }
}

#ifdef LUMA_BENCH_RUN
/* Bounded hardware RAM-run entry.
 *
 * Runs the real display initialisation and one render/present cycle, records a
 * beacon, and RETURNS. Returning is the point: the USB callback that called in
 * still completes, so the host can read the beacon and the DE plane straight
 * back and the machine never needs a power cycle to recover.
 *
 * Only compiled with -DLUMA_BENCH_RUN. The shipped image has no such entry and
 * cannot be entered this way.
 *
 * Deliberately skips sd_hw_init/fat_mount: the stock firmware owns the card
 * over USB while this runs, and a bounded display check does not need storage.
 */
volatile u32 luma_beacon[37] __attribute__((used, section(".data.bench")));
__attribute__((used, noinline, section(".text.bench")))
void luma_bench_once(void *buffer)
{
    App *a=(App *)&luma_app;
    u32 i;
    for (i=0;i<37;i++) { luma_beacon[i]=0; }
    /* Exposure and gain are sweepable from the host so a value can be tried
     * without a rebuild: the frame was arriving saturated, which is an exposure
     * problem rather than a capture problem. The writer is the ops struct's own
     * fp_exp_gain_wr at ops+0xc70. */
    {
        /* Packet input words start at offset 1792 (the harness ABI). */
        volatile u32 *in = (volatile u32 *)((u8 *)buffer + 1792u);
        luma_test_pattern = in[7];
    }
    hardware_init();   /* this now runs the software-IIC sensor bring-up */

    /* Exposure and gain are applied HERE, after the bring-up.
     *
     * They used to be written BEFORE hardware_init(), which meant the function
     * pointer at ops+0xc70 was read out of an unpopulated struct -- the call was
     * guarded by `if (wr)` and silently did nothing. Measured: exposures 0x04,
     * 0x40 and 0x100 all produced the same plane brightness (90.2 / 91.9 / 90.9),
     * proving the write never took effect. The sensor therefore ran at whatever
     * exposure its init table left, which is why the capture was a flat, bright,
     * blown-out field while the stock's own preview is dark (rows 0-119 mean
     * 21-34) and carries real vertical detail.
     *
     * The ops struct at 0x020e32a0 is populated by sensor_init via sensor_bringup,
     * so by this point ops+0xc70 is the real fp_exp_gain_wr. */
    {
        volatile u32 *in = (volatile u32 *)((u8 *)buffer + 1792u);
        u32 exp = in[5], gain = in[6];
        if (exp) {
            void (*wr)(u32, u32) = *(void (**)(u32, u32))(0x020e32a0u + 0xc70u);
            if (wr) { wr(exp, gain ? gain : 0x100u); }
        }
    }
    /* Let the new exposure take effect before the frame is read. */
    for (volatile unsigned s = 0; s < 200000u; ++s) { lcd_service(); csi_service(); }
    /* Which branch did the VENDOR autodetect take? sensor_autocheck (0x02058170)
     * loads the real H63P ops struct to 0x020e32a0 and the 100-entry init table
     * to 0x020d1ea0 ONLY when its own ID probe matches. Otherwise it quietly
     * substitutes an internal test pattern and returns 0x02083d00. If we land on
     * the fallback, sensor_init feeds the CSI from a test pattern and the imager
     * is never streamed -- which would look exactly like a blank, event-free
     * capture. These three words say which world we are in. */
    luma_beacon[31]=*(volatile u32 *)0x020d2ea0u;  /* ident: 0x020e4ba0=H63P, 0x02083cb0=fallback */
    luma_beacon[32]=*(volatile u32 *)0x020d1e84u;  /* adapter chosen by autocheck */
    luma_beacon[33]=*(volatile u32 *)0x020d2ea4u;  /* active ident */
    luma_beacon[10]=(u32)sensor_id_ack;
    luma_beacon[11]=(u32)sensor_id;
    luma_beacon[12]=(u32)sensor_table_ok;
    luma_beacon[13]=sensor_raw_ack;
    luma_beacon[14]=sensor_dbg_slave;
    luma_beacon[15]=sensor_dbg_reg;
    luma_beacon[16]=sensor_dbg_read;
    luma_beacon[17]=sensor_dbg_rx;
    luma_beacon[18]=sensor_tab_written;
    luma_beacon[19]=sensor_tab_fail_idx;
    luma_beacon[20]=sensor_tab_fail_reg;
    luma_beacon[21]=sensor_tab_fail_val;
    luma_beacon[22]=sensor_tab_fail_stage;
    luma_beacon[23]=sensor_dbg_rx;
    /* CSI state AFTER the bring-up. If b000 is not enabled, or b018 shows no
     * transfer, the capture path was never armed and no frame can appear --
     * which the raw CSI buffers already suggested. */
    luma_beacon[24]=REG(0xb000);
    luma_beacon[25]=REG(0xb018);
    luma_beacon[26]=REG(0xb00c);
    luma_beacon[27]=REG(0xb014);
    /* Filled in after the spin: which CSI events arrived, and whether the re-arm
     * guard ever passed. */
    luma_beacon[28]=0;
    luma_beacon[29]=0;
    luma_beacon[30]=0;
    app_init(a,&platform,0);
    /* Render the CAMERA screen, not the boot animation. app_init sets booting=1,
     * so without clearing it every capture so far has been the boot screen and
     * never exercised the viewport at all. */
    a->booting=0;
    a->screen=CAMERA;
    /* Give the capture pipeline time to deliver a frame before reading it.
     *
     * The CSI MUST be serviced here. The real main loop does this at
     * `csi_service()` in firmware_main, but the bounded entry used to spin on
     * lcd_service() alone -- so the CSI transfer was armed once, never
     * acknowledged and never re-armed. It completed and stalled with the arm bit
     * (b018 0x40000) clear and the plane holding whatever that single transfer
     * produced, which was blank because sensor_bringup() runs later in
     * hardware_init and the imager was not streaming yet. That is why every
     * bounded run reported an all-fill or all-zero capture plane. */
    /* Settle for a wall-clock duration, not a fixed spin count.
     *
     * This loop used to run 4,000,000 iterations of lcd_service()/csi_service(),
     * which is not a known amount of TIME and in practice was not long enough: the
     * digest below then measured an early frame, while the panel -- which runs the
     * real firmware_main loop for seconds under takeover -- showed a converged,
     * live scene. Every experiment was being judged against the wrong moment.
     *
     * Waits on the real millisecond clock instead, and reports the elapsed time
     * back through luma_clock_delta (out[25] as clock_ms_across_spin) so the host
     * can confirm the settle actually happened. */
    {
        AxTime t={0,0,0};
        u32 t0=ax_time_ms(&t,REG(0x9908));
        for (;;) {
            lcd_service();
            csi_service();
            luma_clock_delta = ax_time_ms(&t,REG(0x9908)) - t0;
            if (luma_clock_delta >= 3000u) { break; }
        }
    }
    luma_beacon[28]=luma_csi_seen;       /* OR of every b00c value seen      */
    luma_beacon[29]=luma_csi_calls;      /* polls that found events          */
    luma_beacon[30]=luma_csi_rearms;     /* re-arms performed                */
    /* Is the imager actually clocked? sensor_init calls csi_set_mclk(ops+0x04).
     * If that field is zero or nonsense the imager never outputs pixels, the DVP
     * input idles, and the ISP dithers it -- a static grey field, which is what
     * the captured plane shows. 0x884c bits 12:8 are the divider that resulted. */
    luma_beacon[35]=*(volatile u32 *)(0x020e32a0u + 0x04u);   /* ops->mclk    */
    luma_beacon[36]=REG(0x884c);                              /* MCLK divider */
    sensor_copy();
    app_render(a,frame,camera,photo,0,0,0);
    present(frame);
    luma_beacon[0]=0x4C554D42u;                 /* 'LUMB' */
    luma_beacon[1]=(u32)lcd_y;
    luma_beacon[2]=(u32)lcd_uv;
    luma_beacon[3]=REG(0xc800);                 /* DE frame control */
    luma_beacon[4]=REG(0xc828);                 /* DE video Y */
    luma_beacon[5]=REG(0xc830);                 /* DE video UV */
    luma_beacon[6]=REG(0xa840)|(REG(0xa844)<<16);
    luma_beacon[7]=(u32)sensor_id;
    luma_beacon[8]=luma_clock_delta;   /* ms advanced across a bounded spin */
    {
        /* Record the keypad ladder so a bench run can show the ADC path works.
         * The idle level is what a run without a pressed button can prove. */
        u32 smin=0xffffffffu, smax=0;
        unsigned k;
        for (k=0;k<32;k++) {
            u32 v=key_sample();
            if (v<smin) { smin=v; }
            if (v>smax) { smax=v; }
        }
        luma_beacon[9]=(smin<<16)|(smax&0xffffu);
    }
}
#endif
