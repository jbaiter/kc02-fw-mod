/* SPDX-License-Identifier: MIT
 * QEMU bring-up HAL. Register contracts: vendor AX3292 SDK ax329x.h and
 * docs/BOARD_RE.md. This is NOT a silicon-ready cold boot or printer driver.
 * Missing: clocks/SDRAM tuning, pinmux, panel timing/commands, sensor tables,
 * USB controller and exception/interrupt entry. Do not flash this RAM image.
 */
#include "luma.h"
#include "paint.h"
#include "storage.h"
#include "input.h"
#include "sd_hw.h"
#include "ax_time.h"
#include "ax_sensor.h"
#include "ax_csi_capture.h"
#include "stock_photo.h"
#include "ax_jpeg_capture.h"
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
#ifdef LUMA_BENCH_RUN
static u32 luma_photos_enabled, luma_jpeg_check, luma_jpeg_view_ok;
/* Defined by the RAM-only JPEG probe include, far below, but the shutter's save
 * hook sits above it. */
static int luma_capture_photo(void);
static int luma_jpeg_owned;
AxJpegOutput luma_jpeg_output __attribute__((aligned(32)));
static AxJpeg luma_jpeg_state;
#endif
/* Panel geometry, measured on the original KC02 in stock USB mode: the DE video
 * plane is 240x320 with a 256-byte pitch (stride register 0x40 words), not the
 * 240-byte pitch a straight 240-wide plane would suggest. Using 240 was never
 * observed working on hardware. */
#define SENSOR_COLS 320u   /* imager output width, measured */
#define SENSOR_ROWS 180u   /* imager output height, measured */
#define LCD_PITCH 256u
#define LCD_ROWS  320u
#define LCD_UVROWS (LCD_ROWS/2u)
/* The DE scans one pair while present() fills the other. Do not reuse a
 * submitted pair until c800's commit clears AND c87c confirms its active Y. */
static u8 lcd_y[2][LCD_ROWS*LCD_PITCH] __attribute__((aligned(32)));
static u8 lcd_uv[2][LCD_UVROWS*LCD_PITCH] __attribute__((aligned(32)));
static unsigned lcd_submitted;
static int lcd_diagnostics;
static u32 lcd_key_level;
volatile u32 luma_de_timeouts;
/* DMA targets have guards immediately after the expected frame, plus bench-only
 * spill space. CPU snapshots have no hardware writer and remain stable over USB.
 * This separation matters: with LSEN off, CSI wrote image bytes over scalars
 * following the old arrays (hardware evidence in capture-audit). */
#define SEN_Y_BYTES AX_CSI_Y_BYTES
#define SEN_UV_BYTES AX_CSI_UV_BYTES
static AxCsiY sensor_dma_y __attribute__((aligned(32)));
static AxCsiUv sensor_dma_uv __attribute__((aligned(32)));
static u8 sensor_y[SEN_Y_BYTES] __attribute__((aligned(32)));
static u8 sensor_uv[SEN_UV_BYTES] __attribute__((aligned(32)));
volatile u32 luma_csi_fault;
/* CSI servicing counters, available to the bounded entry and panel overlay. */
volatile u32 luma_csi_seen, luma_csi_calls, luma_csi_rearms, luma_csi_rearm_armed;
volatile u32 luma_csi_frame_ends;
#ifndef LUMA_BENCH_RUN
static u8 dma[512] __attribute__((aligned(32)));
static SdHw sd;
static Fat fs;
static unsigned photo_ids[GALLERY_MAX], count;
#endif
volatile u32 luma_clock_delta;
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
#ifndef LUMA_BENCH_RUN
static int read_sector(u32 lba, u8 *out) { return sd_hw_read(&sd, lba, out); }
static int write_sector(u32 lba, const u8 *in) { return sd_hw_write(&sd, lba, in); }
#endif
static int read_file(const char *name, u8 *out, unsigned cap, unsigned *n)
{
#ifdef LUMA_BENCH_RUN
    /* Preserve the verified RAM-run theme; stock SD access is photo-only. */
    (void)name; (void)out; (void)cap; (void)n;
    return 0;
#else
    return fat_read(&fs,name,out,cap,n);
#endif
}
static unsigned photo_count(void)
{
#ifdef LUMA_BENCH_RUN
    return stock_photo_count();
#else
    count=fat_photos(&fs,photo_ids,GALLERY_MAX);
    return count;
#endif
}
static int load_photo(unsigned index, u16 *out)
{
#ifdef LUMA_BENCH_RUN
    return stock_photo_load(index,out);
#else
    char name[13]; unsigned n;
    if (index>=count) { return 0; }
    photo_name(name,photo_ids[index]);
    if (!read_file(name,bitmap,sizeof(bitmap),&n)) { return 0; }
    return (int)photo_bmp_read(bitmap,n,out);
#endif
}
static int save_photo(const u16 *pixels)
{
#ifdef LUMA_BENCH_RUN
    /* A photo is the sensor's own 1280x720, not the 320x180 preview on screen, so
     * the framebuffer is not the source: encode a still with the hardware JPEG
     * encoder and queue those bytes. `pixels` is unused on purpose. */
    (void)pixels;
    return luma_capture_photo();
#else
    char name[13];
    unsigned id=1, i, n;
    photo_count();
    if (count==GALLERY_MAX) { return 0; }
    for (i=0; i<count; i++) { if (id<=photo_ids[i]) { id=photo_ids[i]+1; } }
    if (id>99999) { return 0; }
    photo_name(name,id);
    n=photo_bmp_write(pixels,bitmap,sizeof(bitmap));
    return n && fat_create(&fs,name,bitmap,n);
#endif
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
/* Packet word 13: override CSI_IMAGE_SIZE (b01c) after the bring-up, so the
 * geometry the CSI expects can be swept from the host instead of rebuilt. The
 * capture occupies only the bottom third of the frame, which is what a mismatch
 * between the imager's real output height and this register would look like. */
volatile u32 luma_csi_size_override;

/* Convert the CPU-owned capture snapshot; csi_service() updates it before this
 * function runs. The test pattern bypasses capture conversion, not CSI service. */

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
            /* This is the ISP's planar output, not the sensor's input format.
             * CSIMODE 0x0440062d has FORMAT=0 (raw), not YUVR422; see the actual
             * ax32xx_csiTypeSet at 0x02034600. Stock hal_lcdSwitchFrame2 computes
             * its UV offset with y/2, consistent with the 4:2:0 layout used here.
             * Keep the existing limited-range conversion pending ISP calibration
             * verification. Rows below the 320x180 capture are letterboxed. */
            const u8 *sy = sensor_y;
            const u8 *su = sensor_uv;
            int l = y < SENSOR_ROWS ? sy[y*LW+x] : 16;
            unsigned uvrow = y/2;
            int u = (y < SENSOR_ROWS ? su[uvrow*LW+(x&~1u)] : 128) - 128;
            int v = (y < SENSOR_ROWS ? su[uvrow*LW+(x&~1u)+1] : 128) - 128;
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

/* Poll the normal frame-completion path, not the ISR's sensor-error recovery.
 *
 * The actual frame callback is hal_lcdSwitchFrame2 (0x0203a2f0), registered by
 * lcd_panel_init with CSI event 1. Even when it reuses the current buffer it
 * calls csiLCDFrameSet(Y, UV), then csiLCDDmaKick (0x020349b4): OR LCDEN, with no
 * clear and no PIP reset. The clear/reset/set sequence in csiIRQHandler is only
 * for a SENSOR_SIZE_ERR while LCDEN is still set, not normal frame completion.
 * Luma retains a CPU snapshot so the reader never needs the live destination.
 */
static void csi_service(void)
{
#ifdef LUMA_BENCH_RUN
    if (luma_jpeg_owned) { return; }
#endif
    u32 fault = ax_csi_capture_faults(&sensor_dma_y, &sensor_dma_uv);
    if (fault || luma_csi_fault) {
        luma_csi_fault |= fault;
        REG(0xb018) &= ~0x40000u;
        REG(0xb000) &= ~1u;
        return;
    }
    u32 events = REG(0xb00c);
    if (!events) { return; }
    luma_csi_seen |= events;
    luma_csi_calls++;
    /* Acknowledge before starting the next transfer, as the stock ISR does
     * before invoking its frame callback. Do not clear a new frame's event. */
    REG(0xb00c) = events;

    if (events & 0x10u) {
        REG(0xb000) = (REG(0xb000) & 0xf3ffffffu) | 0x04000000u;
    }
    if (events & 0x0002u) {
        luma_csi_frame_ends++;
        memcpy(sensor_y, sensor_dma_y.data, SEN_Y_BYTES);
        memcpy(sensor_uv, sensor_dma_uv.data, SEN_UV_BYTES);
        /* Re-establish the frame's base addresses before the manual kick. */
        REG(0xb060) = (u32)sensor_dma_y.data;
        REG(0xb064) = (u32)sensor_dma_uv.data;
        REG(0xb018) |= 0x40000u;
        luma_csi_rearms++;
    }
}

static void present(const u16 *f)
{
    unsigned x,y,next,guard;
    if (lcd_submitted > 1u) { luma_de_timeouts++; return; }
    u32 expected = (u32)lcd_y[lcd_submitted] & 0xffffffu;
    /* Stock videoGetYAddr (0x0203367c) reads c87c & 0xffffff. A cleared
     * request alone is not proof that the expected buffer is now active. */
    for (guard=0; guard<200000u; ++guard) {
        if (!(REG(0xc800) & 2u) &&
            (REG(0xc87c) & 0xffffffu) == expected) { break; }
        lcd_service();
    }
    if (guard == 200000u) { luma_de_timeouts++; return; }
    next = lcd_submitted ^ 1u;
    for (y=0; y<320; y++) {
        for (x=0; x<240; x++) {
            u16 c=BOARD_ID==1 ? f[(239-x)*LW+y] : f[x*LW+319-y];
            int r=(c>>11)*255/31, g=((c>>5)&63)*255/63, b=(c&31)*255/31;
            lcd_y[next][y*LCD_PITCH+x]=(77*r+150*g+29*b)>>8;
            if (!(x&1) && !(y&1)) {
                lcd_uv[next][(y/2)*LCD_PITCH+x]=clip(128+((-43*r-85*g+128*b)>>8));
                lcd_uv[next][(y/2)*LCD_PITCH+x+1]=clip(128+((128*r-107*g-21*b)>>8));
            }
        }
    }
    /* Diagnostics also belong in the inactive buffer, BEFORE submission. */
    if (lcd_diagnostics) {
        unsigned mark = (unsigned)((luma_frames + 1u) % 240u);
        unsigned bar = lcd_key_level == 0xffffffffu ? 0u : lcd_key_level / 4u;
        if (bar > 240u) { bar = 240u; }
        for (x=0; x<240; ++x) {
            lcd_y[next][5u*LCD_PITCH+x] = x == mark ? 255u : 0u;
            lcd_y[next][8u*LCD_PITCH+x] = x < bar ? 200u : 0u;
        }
    }
    /* Stock videoSetAddr (0x020336dc), then deUpdate. The addresses are staged;
     * the active plane remains untouched until the frame-boundary latch. */
    REG(0xc828) = (u32)lcd_y[next];
    REG(0xc830) = (u32)lcd_uv[next];
    REG(0xc800) |= 3u;
    lcd_submitted = next;
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
    /* Stop the inherited capture BEFORE changing its buffers or geometry. The
     * output is armed only after sensor, scaler and guards are ready below. */
    REG(0xb018) &= ~0x40000u;
    REG(0xb000) &= ~1u;
    /* Initialise both display buffers before asking the DE to scan either. */
    memset(lcd_y, 0x10, sizeof(lcd_y));
    memset(lcd_uv, 0x80, sizeof(lcd_uv));
    lcd_submitted = 0;
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
    REG(0xc828)=(u32)lcd_y[0]; REG(0xc82c)=LCD_PITCH/4;
    REG(0xc830)=(u32)lcd_uv[0]; REG(0xc834)=LCD_PITCH/4;
    /* Commit the video registers once. Measured on original KC02: a staged
     * c828 does not become the active address c87c until DE_FRAME_CTRL bit 1
     * is raised, and the change completes at a frame boundary (bit 1 then
     * self-clears). Without this the first framebuffer never takes effect.
     * This is the SDK's ax32xx_deUpdate. */
    REG(0xc800)=1|2;
    /* Luma draws straight into its own DE plane, so every OTHER writer into the
     * display path has to be off. Measured on real hardware, the stock firmware
     * leaves the rotate engine enabled with its target on the DE video plane
     * (ROTATECON = 5 and ROTTGYADR == c828), and it leaves the CSI-to-LCD DMA
     * configured. Either one keeps writing into the plane Luma is displaying,
     * which shows up as a bright block rolling across the UI. */
    REG(0x898c)=0;   /* rotate engine off: Luma does not compose through it */
    /* Configure input with LCD DMA disabled. Starting it here, before sensor
     * bring-up and scaler enable, allowed unscaled frames to overwrite Luma RAM.
     * Keep the measured input mode/size; csiLCDScaler sets the output below. */
    REG(0x8840) &= ~(1u << 27);
    REG(0xb000)=0x0440062cu;  /* CSI_ENABLE clear */
    REG(0xb014)=0x800ef87bu;
    REG(0xb018)=0x00200000u;  /* manual mode, LCDEN clear */
    REG(0xb01c)=0x02cf04ffu;
    REG(0xb078)=0x02d00500u;
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
    memset(sensor_y, 0x10, sizeof(sensor_y));
    memset(sensor_uv, 0x80, sizeof(sensor_uv));
    memset(sensor_dma_y.data, 0x10, sizeof(sensor_dma_y.data));
    memset(sensor_dma_uv.data, 0x80, sizeof(sensor_dma_uv.data));
#ifdef LUMA_BENCH_RUN
    memset(sensor_dma_y.spill, 0xa5, sizeof(sensor_dma_y.spill));
    memset(sensor_dma_uv.spill, 0xa5, sizeof(sensor_dma_uv.spill));
#endif
    ax_csi_guards_init(&sensor_dma_y, &sensor_dma_uv);
    if (!sensor_bringup()) { luma_csi_fault = 0x1000u; return; }

    /* sensor_init only registers the sensor; it does NOT enable the line scaler.
     * Call the actual stock recipe (0x020349f8), as hal_lcdPreviewStart does.
     * It enables LCDCROP and LSEN, installs the panel's coefficient table, sets
     * target/ratios/stride and kicks LS_KS. Merely setting the size registers
     * while b004 remained 0xffff left scaling OFF and overflowed our buffers. */
    {
        typedef void (*scaler_fn)(u16,u16,u16,u16,u16,u16,u16,u16,u16,u32 *);
        u32 panel = *(volatile u32 *)0x020ce980u;
        if (panel < 0x02000000u || panel > 0x027ffe00u) {
            luma_csi_fault = 0x100u;
            return;
        }
        ((scaler_fn)0x020349f8u)(1280,720,SENSOR_COLS,SENSOR_ROWS,
                                0,0,1280,720,SENSOR_COLS,(u32 *)(panel + 0xd0u));
    }
    REG(0xb060)=(u32)sensor_dma_y.data;
    REG(0xb064)=(u32)sensor_dma_uv.data;
    if (luma_csi_size_override) { REG(0xb01c) = luma_csi_size_override; }
    /* Core frame clocks let the scaler latch; output DMA is still disabled. */
    REG(0xb000) |= 1u;
    {
        u32 g;
        for (g=0; g<0x400000u && (REG(0xb008) & 0x80000u); ++g) { }
        if (REG(0xb008) & 0x80000u) { luma_csi_fault = 0x200u; return; }
    }
    if ((REG(0xb004) & 0x420000u) != 0x420000u ||
        REG(0xb180) != ((SENSOR_ROWS<<16)|SENSOR_COLS) ||
        REG(0xb068) != SENSOR_COLS) {
        luma_csi_fault = 0x400u;
        return;
    }
    REG(0xb00c)=0x5au;
    REG(0xb014) |= 0x8008005au;
    REG(0xb018) |= 0x40000u;
    lens(0);
}

#ifdef LUMA_BENCH_RUN
#include "ax3292_jpeg.inc"
#endif

/* ---------------------------------------------------------------------------
 * ON-PANEL CAPTURE TELEMETRY
 *
 * Why this exists: a takeover run never returns to the host, so there is no USB
 * readback and the beacon is unreachable. That would not matter if the bounded-run
 * digest could be trusted, but it provably cannot -- it reported two flat fields
 * while the panel showed a live scene, and three separate conclusions (all-zero
 * planes, untouched planes, the row-120 height theory) were drawn from it and were
 * all wrong. The panel is the only channel that reports the steady state, so the
 * numbers have to go on the screen.
 *
 *   Y / UV  bytes carrying real data: neither the init fill (0x10 / 0x80) nor zero
 *   EV      OR of every CSI_INTR (b00c) value seen -- which events actually fire
 *   B018    CSI_DMA_MODE; bit 19 (0x40000) is CSI_OUTPUT_LCDEN, the "arm" bit
 *   POLL    polls that found an event
 *   REAR    re-arms performed
 *   FRAME   frames rendered by this loop
 *
 * Gated on packet word 11 so it can be switched on for a measurement run without
 * covering the viewfinder the rest of the time.
 */
volatile u32 luma_overlay_on;
static u32 ov_ydata, ov_uvdata, ov_seen, ov_calls, ov_rearms, ov_b018, ov_frames;

static char *ov_str(char *o, const char *s) { while (*s) { *o++ = *s++; } return o; }
static char *ov_dec(char *o, u32 v)
{
    char t[12]; int n = 0;
    if (!v) { *o++ = '0'; return o; }
    while (v) { t[n++] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    while (n) { *o++ = t[--n]; }
    return o;
}
static char *ov_hex(char *o, u32 v, int digits)
{
    static const char h[] = "0123456789ABCDEF";
    int i;
    for (i = digits - 1; i >= 0; --i) { *o++ = h[(v >> (4 * i)) & 0xfu]; }
    return o;
}

/* Recounts ~86 KB of planes, so it runs every 120 frames rather than per frame. */
static void overlay_refresh(void)
{
    u32 i, yd = 0, ud = 0;
    const u8 *sy = sensor_y;
    const u8 *su = sensor_uv;
    for (i = 0; i < SEN_Y_BYTES; ++i)  { u8 v = sy[i]; if (v != 0x10u && v != 0u) { yd++; } }
    for (i = 0; i < SEN_UV_BYTES; ++i) { u8 v = su[i]; if (v != 0x80u && v != 0u) { ud++; } }

    ov_ydata = yd;  ov_uvdata = ud;
    ov_seen = luma_csi_seen;   ov_calls  = luma_csi_calls;
    ov_rearms = luma_csi_rearms; ov_b018 = REG(0xb018);
    ov_frames = luma_frames;
}

static void overlay_draw(u16 *f)
{
    char b[96], *o;
    int y = 2;
    /* SMALL glyphs, and a compact strip. This is opt-in (packet word 11 /
     * --overlay) and must NOT cover the viewfinder: an earlier large-font version
     * took the top 118 of 240 rows and hid half the sensor image, which is the one
     * thing the panel is there to show. The numbers are also available from a
     * bounded run now that it is representative, so the panel copy is a fallback,
     * not the primary instrument. */
    box(f, 0, 0, LW, 62, RGB(0, 0, 0));
    o = b; o = ov_str(o, "Y");  o = ov_dec(o, ov_ydata);
    o = ov_str(o, " U");        o = ov_dec(o, ov_uvdata); *o = 0;
    ui_text(f, 3, y, b, RGB(255,255,0), 0); y += 15;
    o = b; o = ov_str(o, "EV"); o = ov_hex(o, ov_seen, 4);
    o = ov_str(o, " B");        o = ov_hex(o, ov_b018, 6); *o = 0;
    ui_text(f, 3, y, b, RGB(255,255,0), 0); y += 15;
    o = b; o = ov_str(o, "FE"); o = ov_dec(o, luma_csi_frame_ends);
    o = ov_str(o, " R");        o = ov_dec(o, ov_rearms); *o = 0;
    ui_text(f, 3, y, b, RGB(255,255,0), 0); y += 15;
    o = b; o = ov_str(o, "F");  o = ov_dec(o, ov_frames); *o = 0;
    ui_text(f, 3, y, b, RGB(255,255,0), 0);
}

#ifdef LUMA_BENCH_RUN
/* _start calls firmware_main() with no arguments, so in a TAKEOVER run -- where the
 * bench entry never executes -- this is the only way the loop learns that telemetry
 * was requested. The harness always places the 2048-byte packet at 0x020D8800 and
 * its input words at +1792 (tools/toolchain_tests/payloads/common.h). */
#define BENCH_PACKET_IN ((volatile u32 *)0x020D8F00u)

/* show_luma.py reserves word 12 and puts the size override in word 13. */
static void bench_read_options(const volatile u32 *in)
{
    luma_test_pattern = in[7];
    luma_overlay_on = in[11];
    luma_csi_size_override = in[13];
    luma_photos_enabled = in[14] == STOCK_PHOTO_WRITE_TOKEN;
    luma_jpeg_check = in[15] == AX_JPEG_CHECK_TOKEN;
}

/* sensor_init populates this pointer. Apply exposure after hardware_init in
 * BOTH entry paths: bounded captures must use the same settings as takeover. */
static void bench_apply_exposure(const volatile u32 *in)
{
    u32 exp = in[5], gain = in[6];
    if (exp) {
        void (*wr)(u32, u32) = *(void (**)(u32, u32))(0x020e32a0u + 0xc70u);
        if (wr) { wr(exp, gain ? gain : 0x100u); }
    }
}
#endif

void firmware_main(void)
{
    App *a=(App *)&luma_app;
#ifdef LUMA_BENCH_RUN
    bench_read_options(BENCH_PACKET_IN);
#endif
    Input input={0};
    AxTime clock={0,0,0};
    int have_bg=0, have_boot=0;
    u32 last=~0u;
    unsigned n;
    hardware_init();
#ifdef LUMA_BENCH_RUN
    bench_apply_exposure(BENCH_PACKET_IN);
    /* The encoder's output window doubles as the file staging buffer: a save and
     * a gallery load can never overlap, so one 512 KiB block serves both. */
    stock_photo_init((int)luma_photos_enabled,luma_jpeg_output.bytes,AX_JPEG_CAPACITY,
                     0,&jpeg_view);
#endif
    app_init(a,&platform,ax_time_ms(&clock,REG(0x9908)));
#ifdef LUMA_BENCH_RUN
    /* Start on the CAMERA screen with the boot animation disabled, exactly as the
     * bounded entry does.
     *
     * app_init leaves booting=1, and the boot screen is rendered from theme_boot,
     * which is read off the SD card. A takeover run has no card and no theme, so
     * the panel came up PURE BLACK -- the capture was fine underneath, but there
     * was nothing on screen to see it through. The bounded entry has cleared this
     * since the beginning (see the note there); the main loop never did. */
    a->booting = 0;
    a->screen = CAMERA;
#endif
    /* Draw before touching storage. sd_hw_init can block on a card the host
ows,
     * and a first frame that only appears after storage succeeds is a first frame
     * that never appears when storage misbehaves. On hardware this was exactly the
     * failure: the RAM takeover hung in storage init and the panel stayed frozen
     * on the previous firmware's screen, while the bench entry that skipped
     * storage rendered correctly. */
    app_render(a,frame,camera,photo,0,0,0);
    present(frame);
#ifndef LUMA_BENCH_RUN
    u32 capacity=sd_hw_init(&sd,dma);
    if (capacity) { fat_mount(&fs,read_sector,write_sector,capacity); }
#endif
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
        if (!dirty && now-last < 40) {
#ifdef LUMA_BENCH_RUN
            if (stock_photo_poll(a,now,!(REG(0xc800)&2u))) { dirty=1; }
#endif
            continue;
        }
        dirty=0;
        last=now;
        if (a->screen==CAMERA) { sensor_copy(); }
        app_render(a,frame,camera,photo,have_bg ? theme_bg : 0,have_boot ? theme_boot : 0,now);
        if (luma_overlay_on) {
            if ((luma_frames % 120u) == 0u) { overlay_refresh(); }
            overlay_draw(frame);
        }
        /* present() draws the marker/key diagnostics before its buffer swap. */
        lcd_diagnostics = 1;
        lcd_key_level = key_sample();
        present(frame);
        luma_frames++;
#ifdef LUMA_BENCH_RUN
        if (stock_photo_poll(a,now,!(REG(0xc800)&2u))) { dirty=1; }
#endif
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
volatile u32 luma_beacon[56] __attribute__((used, section(".data.bench")));
__attribute__((used, noinline, section(".text.bench")))
void luma_bench_once(void *buffer)
{
    App *a=(App *)&luma_app;
    u32 i;
    for (i=0;i<56;i++) { luma_beacon[i]=0; }
    /* Packet input starts at +1792, the same ABI as takeover's fixed address. */
    const volatile u32 *in = (const volatile u32 *)((u8 *)buffer + 1792u);
    bench_read_options(in);
    hardware_init();   /* populates fp_exp_gain_wr in the stock sensor ops */

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
    bench_apply_exposure(in);
    /* The encoder's output window doubles as the file staging buffer: a save and
     * a gallery load can never overlap, so one 512 KiB block serves both. */
    stock_photo_init((int)luma_photos_enabled,luma_jpeg_output.bytes,AX_JPEG_CAPACITY,
                     0,&jpeg_view);
    {
        const StockFs *s=stock_photo_state();
        luma_beacon[38]=(u32)s->ready;
        luma_beacon[39]=s->count;
        luma_beacon[40]=(u32)s->last_error;
        luma_beacon[41]=(u32)s->writable;
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
    if (luma_jpeg_check) {
        jpeg_encode_once(in[10]);
        luma_beacon[42]=luma_jpeg_state.state;
        luma_beacon[43]=luma_jpeg_state.fault;
        luma_beacon[44]=luma_jpeg_state.length;
        luma_beacon[45]=luma_jpeg_state.csi_events;
        luma_beacon[46]=luma_jpeg_state.jpeg_events;
        luma_beacon[47]=REG(0x4800);
        /* Phase two: decode the frame we just encoded back to 320x180. This is
         * the whole still loop minus the file write, and it is what proves the
         * gallery can show a sensor-resolution photo at all. */
        if (luma_jpeg_state.state==AX_JPEG_DONE && luma_jpeg_state.length &&
            jpeg_view_to_yuv(luma_jpeg_output.bytes,luma_jpeg_state.length)) {
            luma_beacon[54]=1;
            luma_jpeg_view_ok=1;
        }
        luma_beacon[48]=luma_decode_state.state;
        luma_beacon[49]=luma_decode_state.fault;
        luma_beacon[50]=luma_decode_state.status;
        luma_beacon[51]=luma_decode_geom;
        if (in[10]) {
            volatile u32 *r=(volatile u32 *)(in[10]+512u);
            for (i=0;i<128;i++) r[i]=REG(0xb800u+4u*i);
        }
    }
    /* sensor_y / sensor_uv are CPU snapshots, never live DMA destinations. */
    /* Count the OTHER set as well. If the buffer swap does not take effect, one set
     * is never written and the panel alternates between a fresh frame and a stale
     * one -- blinking, which a single snapshot of the readable set cannot show. */
    {
        u32 o = 0, k2;
        const u8 *so = sensor_dma_y.data;   /* the live write buffer */
        for (k2 = 0; k2 < SEN_Y_BYTES; ++k2) {
            u8 v = so[k2];
            if (v != 0x10u && v != 0u) { o++; }
        }
        luma_beacon[37] = o;
    }
    luma_beacon[28]=luma_csi_seen;       /* OR of every b00c value seen      */
    luma_beacon[29]=luma_csi_calls;      /* polls that found events          */
    luma_beacon[30]=luma_csi_rearms;     /* re-arms performed                */
    /* Is the imager actually clocked? sensor_init calls csi_set_mclk(ops+0x04).
     * If that field is zero or nonsense the imager never outputs pixels, the DVP
     * input idles, and the ISP dithers it -- a static grey field, which is what
     * the captured plane shows. 0x884c bits 12:8 are the divider that resulted. */
    luma_beacon[35]=luma_csi_fault;                          /* bounds/setup faults */
    luma_beacon[36]=REG(0x884c);                              /* MCLK divider */
    sensor_copy();
    if (luma_jpeg_view_ok) {
        /* Show what the decoder produced, not the live preview: a panel verdict on
         * this frame is a verdict on the still pipeline. */
        memcpy(sensor_y,stock_photo_view_y(),AX_DECODE_Y_BYTES);
        memcpy(sensor_uv,stock_photo_view_uv(),AX_DECODE_UV_BYTES);
    }
    app_render(a,frame,camera,photo,0,0,0);
    present(frame);
    luma_beacon[34]=luma_de_timeouts;           /* presenter ownership failures */
    luma_beacon[0]=0x4C554D42u;                 /* 'LUMB' */
    luma_beacon[1]=(u32)lcd_y[lcd_submitted < 2u ? lcd_submitted : 0u];
    luma_beacon[2]=(u32)lcd_uv[lcd_submitted < 2u ? lcd_submitted : 0u];
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
