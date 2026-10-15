/*
 * AX3292 (KC02-family camera) machine.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Bare-metal machine for the dumped firmware.  It provides flat RAM, a
 * bootable application image and an SPR bus; every peripheral on this chip is
 * reached through special-purpose registers rather than memory, so the "bus" is
 * the SPR hook in target/or1k/sys_helper.c.
 *
 * Devices are added on demand, driven by the SPR trace: run the firmware, see
 * which register it blocks on, model that.  Modelled so far:
 *
 *   UART0   (0x9080)  the boot log; TX is instantaneous so the poll succeeds
 *   SPI0    (0x9000)  flash controller, byte mode and DMA, backed by the dump
 *
 * Architectural SPRs are remapped too, because this chip does not use the
 * OpenRISC spec numbering: SR 0x18, NPC 0x14, PPC 0x1C, EPCR 0x20, ESR 0x40,
 * EVBA 0x50, VR 0x00.  See tools/emu/sfr_map.py for the evidence.
 *
 * Reference: docs/EMULATOR_PLAN.md, docs/BOOT_MAP.md and the vendor SDK.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/host-utils.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "exec/memattrs.h"
#include "hw/core/boards.h"
#include "hw/core/cpu.h"
#include "hw/core/loader.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "system/memory.h"
#include "system/reset.h"
#include "target/or1k/cpu.h"
#include "ui/console.h"
#include "ui/input.h"
#include "ax3292-printer.h"
#include "ax3292-adc.h"
#include "ax3292-timer.h"
#include "ax3292-de.h"

#define TYPE_AX3292_MACHINE MACHINE_TYPE_NAME("ax3292")
OBJECT_DECLARE_SIMPLE_TYPE(Ax3292State, AX3292_MACHINE)

/* The boot code (flash file 0x520) sets SP and calls this address. */
#define AX3292_ENTRY        0x0200046C
#define AX3292_STACK_TOP    0x027FFFFC
#define AX3292_SFR_COUNT    (0x10000 / 4)
#define AX3292_SDRAM_BASE   0x02000000
#define AX3292_RAM_SIZE     (64 * MiB)

#define AX3292_FLASH_SIZE   (4 * MiB)

/*
 * Diagnostics and bring-up escape hatches.
 *
 * Everything below is diagnostic: it observes the model or perturbs it for
 * debugging, and none of it is needed to run firmware.  The whole set stays
 * inert unless AX3292_DEBUG=1, so a stray variable cannot change a production
 * run.  Board wiring and boot handoff are machine properties instead; see
 * ax3292_props.
 *
 *   AX3292_DEBUG=1                  enable every knob in this list
 *   AX3292_TRACE_SPR                SPR access trace
 *   AX3292_TRACE_SPR_MIN/MAX/LIMIT  its address range and entry budget
 *   AX3292_TRACE_PC[_MS]            PC/GPR sampler and its period
 *   AX3292_TRACE_PROBE              change-only RAM probes, comma separated
 *   AX3292_TRACE_WATCH[_WORDS]      dump guest words when the tick ISR runs
 *   AX3292_TRACE_SPI/SD/JPEG/KEY/PRINTER   per-device traces
 *   AX3292_DUMP_RAM/SFR             periodic SDRAM / SFR snapshots
 *   AX3292_DUMP_FB[_EVERY]          periodic scanout PPM
 *   AX3292_DEBUG_PULSE_IRQ/_PERIOD  inject one interrupt source
 *   AX3292_DEBUG_FORCE_IRQ0         force PIC source 0
 *   AX3292_DEBUG_LCDC_IRQ           enable/disable the frame interrupt
 *   AX3292_DEBUG_TICK_IRQ           override the tick interrupt line
 *   AX3292_DEBUG_TICK_MS            override the tick period
 *   AX3292_DEBUG_CLOCK              "virtual" selects the virtual clock
 */
static bool ax3292_debug;

static const char *ax3292_dgetenv(const char *name)
{
    const char *value = getenv(name);

    if (value == NULL || strcmp(value, "0") == 0) {
        return NULL;
    }
    if (!ax3292_debug) {
        warn_report("%s ignored; set AX3292_DEBUG=1 to enable diagnostics", name);
        return NULL;
    }
    return value;
}

struct Ax3292State {
    MachineState parent;

    MemoryRegion ram;
    uint8_t *ram_ptr;

    uint32_t entry;
    uint32_t stack_top, vbg_addr;
    bool panel_ccw, sensor_3a, dual_sensor, key_trace;
    unsigned key_channel;
    unsigned key_adc[11];
    bool trace;
    unsigned long trace_limit;
    unsigned long trace_count;
    uint32_t trace_min, trace_max;
    unsigned long tick_count;
    unsigned tick_irq;

    /* UART0 (SFR2 0x9080) */
    uint32_t uart_con;
    uint32_t uart_baud;
    uint32_t uart_pnd;
    uint32_t uart_rx;

    /* RTC (0x88e0): bits 0/1 of RTCCON are event-pending flags; RTCPND acks. */
    uint32_t rtc_con;

    /* LCDSTA (0xa804) event bits.  The LCD ISR at 0x0203584c reads them and
     * clears each by writing the bit *eight positions higher* (status bit 2 is
     * cleared with 0x400, bit 7 with 0x8000).  Clearing bit 2 runs the
     * registered frame-complete callback at *(0x020d7ebc + 8). */
    uint32_t lcd_events;

    /* DAC output-buffer model. */
    uint32_t dac_sta;       /* what DACOBSTA reports */
    bool dac_seen;          /* the firmware has configured the DAC */
    uint32_t eng_sta;       /* the 0x9880 completion bits */
    FILE *sd_image;
    uint64_t sd_size;
    uint32_t sd_response, sd_lba;
    unsigned sd_transfer;
    bool sd_armed, sd_trace;
    int eng_countdown;
    int rot_countdown;      /* ticks until ROTATECON bit 31 clears */
    int rot_irq_pending;    /* ticks until the rotate-completion IRQ (23) */
    bool sensor;            /* the sensor property: fake a camera */
    bool jpeg_a_pending;    /* one-shot JPGA work, completed on the owning CPU */
    bool jpeg_b_pending, jpeg_b_ok;
    int64_t jpeg_b_ready_ns;
    bool jpeg_trace;
    AxPrinter printer;
    unsigned i2c_phase[2];
    bool i2c_selected[2];
    uint8_t i2c_offset[2];
    uint8_t sensor_regs[2][256];
    uint32_t csi_lcd_size;    /* ISP_LS_SIZE committed by ISP_CTRL_LS_KS */
    QemuConsole *console;
    QemuInputHandlerState *input;
    QEMUBH *display_bh;
    uint8_t *screen_pixels;
    unsigned screen_width, screen_height;
    bool rot_irq;           /* report the rotation completion as IRQ 23 */
    /* Set when the firmware enables the display (0x898c bit 31); frame
     * interrupts only make sense from then on, and raising them earlier
     * disturbs the boot. */
    bool lcd_enabled;
    bool lcd_seen;          /* the firmware has initialised the LCD itself */

    /* AX3292_TRACE_PROBE: a handful of guest addresses sampled once every 10
     * ticks (100 ms) and printed whenever they change.  A temporal instrument
     * that costs nothing and -- unlike the SPR trace -- cannot be truncated. */
    /* AX3292_DEBUG_PULSE_IRQ: raise one interrupt line for the first half of
     * every AX3292_DEBUG_PULSE_PERIOD ticks.  A bounded event injector for the
     * interrupt sources the model does not drive (6, 15, 22, 23 ...), so each
     * can be tested without a storm derailing the boot. */
    int pulse_irq;
    int pulse_period;
    int pulse_prev;

    uint64_t probe_tick;
    int probe_n;
    uint32_t probe[8];
    uint32_t probe_prev[8];

    /* ADC (0x9170): channel select + GO, result in ADCDAT.  Channel 3 is the
     * keypad; the value comes from the key-value property (or the key-file
     * property) so keys can be injected while the guest runs. */
    uint32_t wdt_last_r9;
    uint32_t adc_con;
    uint32_t adc_dat;
    Ax3292TickTimer tt;
    char *key_file;
    uint32_t key_value;

    /* Framebuffer capture: AX3292_DUMP_FB=<path> dumps the panel every N ticks. */
    const char *fb_path;
    uint32_t fb_addr;
    unsigned fb_every;

    /* SPI0 flash controller (SFR2 0x9000) */
    uint32_t spi_gcon;
    uint32_t spi_dcon;
    uint32_t spi_baud;
    uint32_t spi_dat;        /* byte-mode data */
    uint32_t spi_sta;
    uint32_t spi_cnt;
    uint32_t spi_badr;
    uint32_t spi_sadr;
    int spi_state;
    uint32_t spi_cmd;
    unsigned spi_dummy;
    uint8_t flash_id[3], flash_uid[8];
    uint32_t spi_pos;
    uint32_t spi_addr;
    bool spi_trace;

    uint8_t *flash;          /* the dump, 4 MiB */
    size_t flash_size;

    /*
     * Generic backing store for the SFR-group registers that have no device
     * model yet.  Reading back what was written matters: the firmware uses
     * read-modify-write on GPIO and on the LCD control registers.
     */
    /* Backing store for the SFR groups.  Bounded: the chip silently ignores
     * accesses to unimplemented SPRs, and the firmware does compute SPR
     * numbers from registers (a 0x100010 write was seen), so an unchecked
     * index here corrupts the device state. */
    uint32_t sfr[AX3292_SFR_COUNT];

    /* Tick timer: the RTOS needs a periodic interrupt to leave its idle loop. */
    OpenRISCCPU *cpu;
    QEMUTimer *tick;
    QEMUClockType tick_clock;
    uint32_t tmr_pnd;
    unsigned tick_ms;
    bool tick_running;

    /* AX3292_TRACE_WATCH=<hex>: dump guest words whenever the tick ISR runs. */
    hwaddr watch;
    unsigned watch_words;
    unsigned watch_count;
    unsigned dump_count;

    /* Alias of the low 64 KiB at 0xf0000000. */
    MemoryRegion low_alias;

    bool force_irq0;         /* optional PIC mask diagnostic */
    bool lcdc_irq;
    QEMUTimer *sampler;
    unsigned sample_ms;

    /* Machine properties (see ax3292_props): board wiring and boot handoff.
     * Everything here is a run input, so it belongs on the command line. */
    uint32_t panel_rotation;
    char *key_values, *extra_keys;
    char *flash_id_str, *flash_uid_str;
    char *flash_path, *sd_path;
    char *printer_directory, *printer_phases_str;
    bool printer_no_paper;
};

/* --- register numbers ----------------------------------------------------- */

enum {
    AX3292_SPR_VR   = 0x00,
    AX3292_SPR_NPC  = 0x14,
    AX3292_SPR_SR   = 0x18,
    AX3292_SPR_PPC  = 0x1C,
    AX3292_SPR_EPCR = 0x20,
    AX3292_SPR_EEAR = 0x30,   /* unconfirmed: or1k spec value, header disagrees */
    AX3292_SPR_ESR  = 0x40,
    AX3292_SPR_EVBA = 0x50,
};

enum {
    AX3292_SPI_GCON   = 0x9000,
    AX3292_SPI_FACON  = 0x9004,
    AX3292_SPI_PACON  = 0x9008,
    AX3292_SPI_DCON   = 0x900C,
    AX3292_SPI_ACT    = 0x9244,
    AX3292_SPI_STA    = 0x9248,
    AX3292_SPI_BAUD   = 0x924C,
    AX3292_SPI_DBUF   = 0x9250,
    AX3292_SPI_DCNT   = 0x9254,
    AX3292_SPI_BADR   = 0x9258,
    AX3292_SPI_SADR   = 0x925C,
};

enum {
    AX3292_UART0_CON  = 0x9080,
    AX3292_UART0_BAUD = 0x9084,
    AX3292_UART0_DAT  = 0x9088,
    AX3292_UART0_PND  = 0x908C,
};

#define AX3292_UART_PND_TX_READY 0x2

/*
 * RTC (0x88e0).  The accessors at 0x0202da98 / 0x0202dae4 write a command to
 * RTCDATA and then poll RTCCON bit 3 *waiting for it to clear* (`l.sfnei r3,0`
 * / `l.bnf exit`) -- i.e. bit 3 is a busy flag, and the generic store (which
 * reads back with bit 3 clear) already reports the part un-busy.
 *
 * NOTE: reading it as *set* (the intuitive "done" reading) makes the poll run
 * to its 0x8000 timeout and stalls the RTC path.
 */
#define AX3292_RTC_CON   0x88E0
#define AX3292_RTC_PND   0x88E4
#define AX3292_RTC_DATA  0x88EC
#define AX3292_RTC_BUSY  0x8
#define AX3292_RTC_EVENTS 0x3

/* ADC, SFR2 */
enum {
    AX3292_ADCCON  = 0x9170,
    AX3292_ADCBAUD = 0x9174,
    AX3292_ADCDAT  = 0x9178,
};
#define AX3292_ADC_EN   0x10
#define AX3292_ADC_EOC  0x40
#define AX3292_ADC_GO   0x80
#define AX3292_ADC_KEY_CH 3

/*
 * Interrupt controller and timers.  The PIC's mask/status registers sit in
 * group 9 (0x4800), which upstream QEMU also models but at different offsets
 * (its PICSR is 0x4802, this chip's is 0x4808), so they are handled here.
 */
enum {
    AX3292_PIC_MR = 0x4800,
    AX3292_PIC_PR = 0x4804,
    AX3292_PIC_SR = 0x4808,
    AX3292_PIC_VR = 0x480C,
    AX3292_PIC_CR = 0x4814,
};

enum {
    AX3292_TMR0_CON = 0x9800,
    AX3292_TMR0_CNT = 0x9804,
    AX3292_TMR0_PR  = 0x9808,
    AX3292_TMR0_PND = 0x98A8,
};

#define AX3292_IRQ_TIMER0  0
#define AX3292_IRQ_RTC     5    /* SDK IRQ_RTC_WDT */
#define AX3292_IRQ_LCDC    4    /* SDK IRQ_LCDC: the display's frame interrupt */
#define AX3292_IRQ_DAC     6    /* SDK IRQ_DAC */
/* The DAC output buffer (SFR2).  The driver's ISR (ax32xx_dacIRQHandler) tests
 * DACOBSTA for bits 1/2/4 and clears each by writing the same bit to DACOBSYNC,
 * then calls the registered handler with that bit as its argument -- which is
 * exactly the event the application's audio state machine at 0x02051fcc
 * dispatches on.  Without the buffer-empty bit the audio never "finishes",
 * media_busy_get() never clears, and the UI never paints.
 *
 * TODO(approximation): this is a completion-signalling model only.  No audio is
 * rendered or captured, and the buffer-empty period is a fixed tick interval. */
#define AX3292_DAC_OCTRL   0x9140   /* bit 2 = OBUF_EMPTY_INT_EN */
#define AX3292_DAC_OSIZE   0x9144
#define AX3292_DAC_OBUFF   0x9148
#define AX3292_DAC_OSYNC   0x914c   /* write 1 to bit N clears status bit N */
#define AX3292_DAC_OSTAUS  0x9150   /* status: 1 = half, 2 = ?, 4 = empty */
#define AX3292_DAC_FIFO    0x9158
#define AX3292_DAC_ADR     0x915c   /* bit31 = busy, bit30 = write, addr<<24 */
#define AX3292_DAC_DAT     0x9160
/* An engine at 0x9880/0x9884 that the firmware starts by writing the control
 * register 0x9884 and then waits on, polling bit 17 of 0x9880 with a timeout
 * taken from the free-running counter at 0x9908 (0x0206874c..0x02068784).
 * Nothing names it in the SDK headers, so it is modelled by behaviour: a
 * started operation completes a couple of ticks later. */
#define AX3292_ENG_CTRL    0x9884
#define AX3292_ENG_STA     0x9880
#define AX3292_ENG_DONE    0x00020000u  /* bit 17 */
/* The rotate/compose engine (SFR1).  The application's display path programs it
 * at 0x02032d58 -- source Y and UV planes, target Y and UV planes, image size
 * and both strides -- and then sets bit 31 of ROTATECON to start it.  The
 * completion poll (0x02032cec) is exactly `mfspr(0x898c) >> 31`, i.e. "still
 * busy", and 0x02044378 retries that ten times before giving up.  Bit 31 must
 * therefore clear, or no frame is ever composed.
 *
 * TODO(approximation): only the YUV420 path is implemented, the busy bit clears
 * synchronously rather than after a pipelined transfer, and chroma is rotated on
 * interleaved pairs rather than as a separate plane. */
#define AX3292_ROT_CON     0x898c   /* bit 31 = busy / start */
#define AX3292_ROT_STA     0x89a8   /* written 1 to start */
#define AX3292_ROT_SRCY    0x8990
#define AX3292_ROT_SRCUV   0x8994
#define AX3292_ROT_TGTY    0x8998
#define AX3292_ROT_TGTUV   0x899c
#define AX3292_IRQ_ROTATE  23       /* SDK IRQ_ROTATE; enabled in the app's PICMR */
/*
 * LCDSTA (0xa804) event bits, from the decompiled handler at 0x0203584c:
 *   0x80 -> cleared with 0x8000, calls one callback
 *   0x04 -> cleared with 0x400,  calls one callback
 *   0x02 -> cleared with 0x200,  calls one callback
 *   0x08 -> cleared with 0x800,  calls one callback AND, when
 *           (0xa800 & 0x30) == 0x10, calls ax32xx_lcdKick() -- the frame kick
 *   0x01 -> cleared with 0x100
 * Every event is cleared by writing the bit eight positions higher, which the
 * write path below implements generically.  Bit 8 is the frame/vsync event; an
 * earlier revision raised bit 2, whose callback is a different one entirely.
 */
/*
 * NOTE: bit 8 is the *real* frame/vsync event -- its callback runs
 * ax32xx_lcdKick() when (0xa800 & 0x30) == 0x10.  Raising it was tried and it
 * REGRESSES the boot (UART drops from 142 B to 56 B and the layer buffers go
 * empty), because lcdKick drives the panel itself and the panel is not
 * emulated.  Bit 2 is therefore raised instead: its callback is a different
 * one, but it keeps the pipeline running far enough to render UI content.
 * Whoever models the panel should switch this back to 0x08.
 */
/*
 * Bit 8 is the true frame/vsync event: its callback runs ax32xx_lcdKick(), which
 * is what makes the firmware re-run the rotate each frame and thus update the
 * composed (destination) buffer.  Bit 2's callback does something else, so the
 * destination never fills.  Bit 8 was previously rejected because it derailed
 * the boot -- it fired before the display pipeline existed -- hence the boot
 * delay gate in the tick handler below.
 */
/*
 * Bit 8 is the TRUE frame/vsync event -- its callback runs ax32xx_lcdKick(),
 * which is what would make the firmware re-run the rotate each frame so the
 * composed destination buffer updates.  Raising it was tried repeatedly,
 * including with a post-boot delay gate and with the LCDCTRL kick made
 * self-clearing, and it consistently derails the boot (UART drops from 142 B to
 * 56 B before the memPool prints, so it fails EARLY and not on timing).  Bit 2's
 * callback is a different one, but the boot stays clean and the preview plane is
 * live, so bit 2 is used until the early-boot interaction with bit 8 is found.
 */
#define AX3292_LCD_EV_FRAME 0x08 /* LCDSTA bit 3: vsync / buffer commit */
#define AX3292_LCD_EV_OTHER 0x80 /* LCDSTA bit 7 */
#define AX3292_TMR_PND_BIT 0x20    /* the ISR clears BIT(5) */
/*: The RTOS tick.  AX3292_TICK_MS=0 disables it. */
#define AX3292_TICK_MS_DEFAULT 10

/* SFR5 LCD block.  Bit 31 of LCDSTA is the operation-complete flag the driver
 * polls (`l.sfgesi r3,0` / `l.bf` around 0x02035244); the model finishes
 * instantly, so it always reads set. */
#define AX3292_LCD_STA 0xA804

/*
 * Frame-completion bits the display path polls (docs/BOARD_RE.md records both):
 * USB20_CSR0 (0xa048) bit 4 and the layer-B control register (0x8910) bit 1.
 * The model has no display engine, so a frame is always finished.
 */
#define AX3292_USB_CSR0   0xA048
#define AX3292_FRAME_DONE 0x10
#define AX3292_LCD_B_CTRL 0x8910
#define AX3292_LCD_B_DONE 0x2

/* SPI flash commands the firmware uses (see the vendor SDK hal_spi.c). */
#define SPI_CMD_WRITE_ENABLE 0x06
#define SPI_CMD_READ_STATUS  0x05
#define SPI_CMD_READ_DATA    0x03
#define SPI_CMD_FAST_READ    0x0B
#define SPI_CMD_READ_ID      0x9F
#define SPI_CMD_READ_UID     0x4B

enum {
    SPI_STATE_IDLE,
    SPI_STATE_ADDR,
    SPI_STATE_READ,
    SPI_STATE_ID,
    SPI_STATE_DUMMY,
    SPI_STATE_UID,
    SPI_STATE_STATUS,
    SPI_STATE_DONE,
};

/* Flash identity is board hardware, supplied independently of guest RAM.
 * Z-12 binds sensor/image resources to its GD25Q32 JEDEC ID and 64-bit UID. */
static void ax3292_hex_identity(const char *text, uint8_t *dest, unsigned length,
                               const char *name)
{
    if (strlen(text) != length * 2) {
        error_report("%s needs exactly %u hex digits", name, length * 2); exit(1);
    }
    for (unsigned i = 0; i < length; i++) {
        int hi = g_ascii_xdigit_value(text[i * 2]);
        int lo = g_ascii_xdigit_value(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            error_report("%s contains a non-hex digit", name); exit(1);
        }
        dest[i] = (hi << 4) | lo;
    }
}

/* Keypad ladders differ per board, so the ADC values are a property.  Values
 * are colon separated because a comma would end the -M option.  A trailing 1024
 * means "no such button"; the base six are limited to 1023. */
static void ax3292_parse_adc_list(const char *text, unsigned *dest, unsigned n,
                                  unsigned maximum, const char *name)
{
    const char *q = text;

    for (unsigned i = 0; i < n; i++) {
        char *end;
        unsigned long value = strtoul(q, &end, 0);
        bool last = i == n - 1;

        if (end == q || value > maximum || (last ? *end != '\0' : *end != ':')) {
            error_report("%s needs %u ADC values (0..%u)%s", name, n, maximum,
                         maximum == 1024 ? ", 1024 = unwired" : "");
            exit(1);
        }
        dest[i] = value;
        q = end + !last;
    }
}

static bool ax3292_spi_enabled(Ax3292State *s)
{
    return s->flash != NULL;
}

/* --- UART0 ---------------------------------------------------------------- */

/*
 * The firmware's send path is the vendor SDK's verbatim:
 *
 *     UART0DAT = byte;
 *     while ((UART0PND & 0x2) == 0) { }
 *     UART0PND |= 1;
 *
 * so bit 1 (transmit complete) has to read as set -- the model has no baud rate
 * and shifts instantly -- and writing ones to UART0PND clears bits.  Output goes
 * to stdout; tools/emu/run.py keeps it apart from the SPR trace.
 */
static void ax3292_uart_tx(uint8_t byte)
{
    static char buf[512];
    static size_t len;

    if (byte == '\r') {
        return;
    }
    buf[len++] = byte;
    if (byte == '\n' || len == sizeof(buf)) {
        fwrite(buf, 1, len, stdout);
        fflush(stdout);
        len = 0;
    }
}

/* --- SPI0 flash ----------------------------------------------------------- */

static void ax3292_spi_reset_xfer(Ax3292State *s)
{
    if (s->spi_state != SPI_STATE_IDLE) {
        fprintf(stderr, "spi: cs edge, xfer reset\n");
    }
    s->spi_state = SPI_STATE_IDLE;
    s->spi_cmd = 0;
    s->spi_pos = 0;
    s->spi_addr = 0;
}

/*: Perform one SPI byte transfer, returning the byte the flash drove. */
static uint8_t ax3292_spi_transfer(Ax3292State *s, uint8_t out)
{
    uint8_t in = 0;

    switch (s->spi_state) {
    case SPI_STATE_IDLE:
        s->spi_cmd = out;
        s->spi_addr = 0;
        s->spi_pos = 0;
        switch (out) {
        case SPI_CMD_READ_DATA:
        case SPI_CMD_FAST_READ:
            s->spi_state = SPI_STATE_ADDR;
            break;
        case SPI_CMD_READ_ID:
            s->spi_state = SPI_STATE_ID;
            break;
        case SPI_CMD_READ_UID:
            s->spi_dummy = 4;
            s->spi_state = SPI_STATE_DUMMY;
            break;
        case SPI_CMD_READ_STATUS:
            s->spi_state = SPI_STATE_STATUS;
            break;
        default:
            s->spi_state = SPI_STATE_DONE;
            break;
        }
        break;

    case SPI_STATE_ADDR:
        s->spi_addr = (s->spi_addr << 8) | out;
        if (++s->spi_pos == 3) {
            s->spi_pos = 0;
            s->spi_dummy = s->spi_cmd == SPI_CMD_FAST_READ ? 1 : 0;
            s->spi_state = s->spi_dummy ? SPI_STATE_DUMMY : SPI_STATE_READ;
        }
        break;

    case SPI_STATE_DUMMY:
        if (--s->spi_dummy == 0) {
            s->spi_pos = 0;
            s->spi_state = s->spi_cmd == SPI_CMD_READ_UID ? SPI_STATE_UID : SPI_STATE_READ;
        }
        break;

    case SPI_STATE_UID:
        in = s->spi_pos < 8 ? s->flash_uid[s->spi_pos] : 0xff;
        s->spi_pos++;
        break;

    case SPI_STATE_READ:
        in = s->flash[(s->spi_addr & (s->flash_size - 1))];
        s->spi_addr++;
        break;

    case SPI_STATE_ID:
        in = s->spi_pos < 3 ? s->flash_id[s->spi_pos] : 0;
        s->spi_pos++;
        break;

    case SPI_STATE_STATUS:
        in = 0;                 /* not busy, no write protection */
        break;

    default:
        break;
    }

    if (s->spi_trace) {
        fprintf(stderr, "spi: xfer out=%02x in=%02x st=%d\n",
                out, in, s->spi_state);
    }
    return in;
}

/*: Bulk read: (SPIFDCNT + 1) * 4 bytes from SPIFSADR to SDRAM SPIFBADR. */
static void ax3292_spi_dma(Ax3292State *s)
{
    uint8_t chunk[4096];
    uint32_t len = (s->spi_cnt + 1) * 4;
    uint32_t src, dst = s->spi_badr;

    /*
     * The vendor SDK sets SPIFSADR = 0x6000000|0 and lets the DMA continue
     * the transfer the byte stream already started: the command and address
     * bytes shifted in through SPIFDBUF are what selects the flash address.
     * Only a standalone DMA (no byte command in flight) uses SPIFSADR.
     */
    if (s->spi_state == SPI_STATE_READ) {
        src = s->spi_addr;
    } else {
        src = s->spi_sadr & 0x01ffffff;
    }

    if (len > s->flash_size) {
        fprintf(stderr, "ax3292: implausible SPI DMA len=%u src=%06x dst=%08x\n",
                len, src, dst);
        len = s->flash_size;
    }

    if (s->spi_trace) {
        fprintf(stderr, "spi: dma len=%u src=%06x dst=%08x\n", len, src, dst);
    }

    s->spi_addr = src + len;

    while (len) {
        uint32_t n = MIN(len, sizeof(chunk));
        uint32_t i;

        for (i = 0; i < n; i++) {
            chunk[i] = s->flash[(src + i) & (s->flash_size - 1)];
        }
        address_space_write(&address_space_memory, dst,
                            MEMTXATTRS_UNSPECIFIED, chunk, n);
        src += n;
        dst += n;
        len -= n;
    }

    s->spi_sta |= 0x2;
}

/* --- timers --------------------------------------------------------------- */

static void ax3292_irq_update(Ax3292State *s);

#include "ax3292-jpeg.h"
#include "ax3292-display.h"
#include "ax3292-sensor.h"
#include "ax3292-sd.h"

static void ax3292_irq_update(Ax3292State *s)
{
    CPUOpenRISCState *env = &s->cpu->env;
    CPUState *cs = CPU(s->cpu);
    bool need_lock = !bql_locked();
    if (need_lock) {
        bql_lock();
    }
    uint32_t pending = env->picsr & env->picmr;
    uint32_t priority = s->sfr[AX3292_PIC_PR >> 2];
    if (pending & priority) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
    if (pending & ~priority) {
        cpu_interrupt(cs, CPU_INTERRUPT_TIMER);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_TIMER);
    }
    if (need_lock) {
        bql_unlock();
    }
}

static void ax3292_tick_cpu(CPUState *cs, run_on_cpu_data data)
{
    Ax3292State *s = data.host_ptr;
    CPUOpenRISCState *env = &s->cpu->env;

    if (!s->tick_running) {
        return;
    }

    s->tmr_pnd |= AX3292_TMR_PND_BIT;
    if (s->tick_irq == AX3292_IRQ_RTC) {
        s->rtc_con |= AX3292_RTC_EVENTS;    /* the RTC ISR reads RTCCON & 3 */
    }
    env->picsr |= 1u << s->tick_irq;

    {
        const char *rd = ax3292_dgetenv("AX3292_DUMP_RAM");
        if (rd != NULL && s->tick_count % 100 == 0) {
            FILE *f = fopen(rd, "wb");
            if (f != NULL) {
                fwrite(s->ram_ptr + 0x02000000, 1, 0x800000, f);
                fclose(f);
            }
        }
    }
    if (ax3292_dgetenv("AX3292_DUMP_SFR") && s->tick_count % 100 == 0) {
        FILE *f = fopen(ax3292_dgetenv("AX3292_DUMP_SFR"), "wb");
        if (f) { fwrite(s->sfr, 1, sizeof(s->sfr), f); fclose(f); }
    }
    if (s->tick_count % 5 == 0) {
        ax3292_scanout(s, s->tick_count % s->fb_every == 0);
    }
    /*
     * The LCDC's frame interrupt.  The firmware enables IRQ4 and IRQ15 as well
     * as the RTC, and the display path's completion handler is the IRQ4 table
     * entry (0x0203584c); nothing else in the model raises it, so the frame
     * never finishes as far as the firmware is concerned.
     */
    /*
     * Frame/vsync events only make sense once the firmware has initialised the
     * LCD.  The gate used to be a write to 0x898c, which is ROTATECON -- the
     * rotate engine, not the display -- so frame interrupts fired during boot,
     * ran a frame callback before the pipeline existed, and derailed the boot
     * (UART 142 B -> 56 B).  Gating on the LCD's own registers instead.
     */
    if (s->lcdc_irq && (s->sfr[0xa800 >> 2] & 1)) {
        s->lcd_events |= AX3292_LCD_EV_FRAME;
        env->picsr |= 1u << AX3292_IRQ_LCDC;
    }

    if (s->pulse_irq >= 0 && s->lcd_enabled) {
        int on = (int)(s->tick_count % (uint64_t)s->pulse_period)
                 < (s->pulse_period / 2 + 1);
        if (on && !s->pulse_prev) {
            env->picsr |= 1u << s->pulse_irq;
        } else if (!on && s->pulse_prev) {
            env->picsr &= ~(1u << s->pulse_irq);
        }
        s->pulse_prev = on;
    }

    if (s->probe_n) {
        uint32_t got[8];
        int i, changed = 0;

        s->probe_tick++;
        for (i = 0; i < s->probe_n; i++) {
            uint32_t off = s->probe[i];
            got[i] = (off + 4 <= AX3292_RAM_SIZE)
                     ? *(uint32_t *)(s->ram_ptr + off) : 0xdeadbeef;
            if (got[i] != s->probe_prev[i]) {
                changed = 1;
            }
        }
        if (changed) {
            fprintf(stderr, "ax3292: probe t=%" PRIu64 " (%" PRIu64 " ms) pc=%08x",
                    s->probe_tick, s->probe_tick * 10, (uint32_t)env->pc);
            for (i = 0; i < s->probe_n; i++) {
                if (got[i] != s->probe_prev[i]) {
                    fprintf(stderr, "  [%08x] %08x -> %08x",
                            s->probe[i], s->probe_prev[i], got[i]);
                    s->probe_prev[i] = got[i];
                }
            }
            fprintf(stderr, "\n");
        }
    }

    /*
     * A configured DAC free-runs: its output buffer empties periodically, and
     * that is the event the application's audio engine is waiting for.  The
     * generated ISR (ax32xx_dacIRQHandler) tests DACOBSTA bits 1/2/4 in order
     * and calls the registered handler with that bit; bit 4 is the "buffer
     * empty" (finished) event, which drives the audio state machine to its
     * done state and lets sfx_play_asset return.  Only bit 4 is raised, so the
     * handler always takes its completion branch.  The guest acks through
     * DACOBSYNC, so this stays level-triggered rather than a storm.
     */
    if (s->eng_countdown > 0 && --s->eng_countdown == 0) {
        s->eng_sta |= AX3292_ENG_DONE;
    }

    if (s->tick_count % 5 == 0) {
        ax3292_sensor_frame(s);
    }
    ax3292_jpeg_a_tick(s);
    ax3292_jpeg_b_tick(s);

    if (s->rot_irq_pending > 0 && --s->rot_irq_pending == 0) {
        /* Completion is a hardware event, independent of guest queues. */
        if (s->sfr[AX3292_ROT_CON >> 2] & 4) {
            env->picsr |= 1u << AX3292_IRQ_ROTATE;
        }
    }

    if (s->rot_countdown > 0 && --s->rot_countdown == 0) {
        s->sfr[AX3292_ROT_CON >> 2] &= ~0x80000000u;   /* no longer busy */
        s->sfr[AX3292_ROT_STA >> 2] = 1;
        if (s->rot_irq) {
            env->picsr |= 1u << AX3292_IRQ_ROTATE;
        }
    }

    if (s->dac_seen && s->tick_count % 5 == 0) {
        s->dac_sta |= 0x04;
        env->picsr |= 1u << AX3292_IRQ_DAC;
    }

    s->tick_count++;
    if (s->trace && (s->tick_count <= 3 || s->tick_count % 200 == 0)) {
        fprintf(stderr, "tick %lu pc=%08x sr=%08x picmr=%08x picsr=%08x "
                "epcr=%08x esr=%08x hp=%u\n",
                s->tick_count, (uint32_t)env->pc, cpu_get_sr(env),
                env->picmr, env->picsr, env->epcr, env->esr,
                env->ax3292_hp_active);
    }

    ax3292_irq_update(s);

    if (s->tick_running && s->tick_ms != 0) {
        timer_mod(s->tick, qemu_clock_get_ms(s->tick_clock) + s->tick_ms);
    }
}

static void ax3292_tick(void *opaque)
{
    Ax3292State *s = opaque;
    /* Peripheral DMA, PIC bits and RAM snapshots run between guest TBs,
     * on the owning vCPU, not concurrently with it in the timer thread. */
    async_run_on_cpu(CPU(s->cpu), ax3292_tick_cpu, RUN_ON_CPU_HOST_PTR(s));
}

static void ax3292_tick_set(Ax3292State *s, bool running)
{
    s->tick_running = running;
    if (running && s->tick_ms != 0) {
        timer_mod(s->tick, qemu_clock_get_ms(s->tick_clock) + s->tick_ms);
    } else {
        timer_del(s->tick);
    }
}

/* --- SPR register model --------------------------------------------------- */

static bool ax3292_reg_read(Ax3292State *s, CPUOpenRISCState *env,
                            uint32_t spr, uint32_t *value);

static bool ax3292_reg_write(Ax3292State *s, CPUOpenRISCState *env,
                             uint32_t spr, uint32_t v);

static bool ax3292_spr_access(struct CPUArchState *env, void *opaque,
                              bool is_write, uint32_t spr, uint32_t *value)
{
    Ax3292State *s = opaque;
    bool handled;

    if (is_write) {
        handled = ax3292_reg_write(s, env, spr, *value);
        if (handled && s->trace && spr >= s->trace_min && spr <= s->trace_max &&
            s->trace_count < s->trace_limit && spr >= 0x8900 && spr < 0x8A00) {
            fprintf(stderr, "lcd W %04x %08x pc=%08x\n",
                    spr, *value, (uint32_t)env->pc);
        }
        if (handled && s->trace && spr >= s->trace_min && spr <= s->trace_max &&
            s->trace_count < s->trace_limit) {
            s->trace_count++;
            fprintf(stderr, "spr W %04x %08x pc=%08x\n",
                    spr, *value, (uint32_t)env->pc);
        }
        return handled;
    }

    handled = ax3292_reg_read(s, env, spr, value);
    if (handled && s->trace && spr >= s->trace_min && spr <= s->trace_max &&
        s->trace_count < s->trace_limit) {
        s->trace_count++;
        fprintf(stderr, "spr R %04x %08x pc=%08x\n",
                spr, *value, (uint32_t)env->pc);
    }
    return handled;
}

static bool ax3292_reg_read(Ax3292State *s, CPUOpenRISCState *env,
                            uint32_t spr, uint32_t *value)
{
    switch (spr) {
    case 0x1800:                       /* AX3292 DCCR (not upstream SPR layout) */
    case 0x2000:                       /* ICCR */
        /* TODO(approximation): cache-control read-back only. TCG memory/DMA
         * is coherent; this does not exercise silicon cache-line operations.
         * Disabled at reset, like the cache-free ROM/application handoff. */
        *value = s->sfr[spr >> 2];
        return true;
    case AX3292_SPR_VR:
        *value = 0x65803200;    /* observed on hardware */
        return true;
    case AX3292_SPR_NPC:
        *value = (uint32_t)env->pc;
        return true;
    case AX3292_SPR_SR:
        *value = cpu_get_sr(env);
        return true;
    case AX3292_SPR_PPC:
        *value = env->ppc;
        return true;
    case AX3292_SPR_EPCR:
        *value = env->epcr;
        return true;
    case AX3292_SPR_EEAR:
        *value = env->eear;
        return true;
    case AX3292_SPR_ESR:
        *value = env->esr;
        return true;
    case AX3292_SPR_EVBA:
        *value = env->evbar;
        return true;

    case AX3292_UART0_CON:
        *value = s->uart_con;
        return true;
    case AX3292_UART0_BAUD:
        *value = s->uart_baud;
        return true;
    case AX3292_UART0_DAT:
        *value = s->uart_rx;
        return true;
    case AX3292_UART0_PND:
        /* TX is instantaneous, so bit 1 always reads set. */
        *value = s->uart_pnd | AX3292_UART_PND_TX_READY;
        return true;

    case AX3292_SPI_GCON:
        *value = s->spi_gcon;
        return true;
    case AX3292_SPI_DCON:
        *value = s->spi_dcon;
        return true;
    case AX3292_SPI_ACT:
    case AX3292_SPI_FACON:
    case AX3292_SPI_PACON:
        *value = 0;
        return true;
    case AX3292_SPI_STA:
        /* Bit 0 is the byte-transfer done flag; the engine is instant. */
        *value = s->spi_sta | 0x1;
        return true;
    case AX3292_SPI_BAUD:
        *value = s->spi_baud;
        return true;
    case AX3292_SPI_DBUF:
        *value = s->spi_dat;
        return true;
    case AX3292_SPI_DCNT:
        *value = s->spi_cnt;
        return true;
    case AX3292_SPI_BADR:
        *value = s->spi_badr;
        return true;
    case AX3292_SPI_SADR:
        *value = s->spi_sadr;
        return true;

    case 0x880C:                        /* WDTCON: the watchdog kick poll */
        *value = s->sfr[spr >> 2];
        if (s->watch != 0) {
            uint32_t r9 = cpu_get_gpr(env, 9);
            if (r9 != s->wdt_last_r9) {
                s->wdt_last_r9 = r9;
                fprintf(stderr, "wdt caller r9=%08x r1=%08x\n", r9,
                        cpu_get_gpr(env, 1));
            }
        }
        return true;

    case AX3292_ADCCON:
        *value = s->adc_con;
        return true;
    case AX3292_ADCDAT:
        *value = s->adc_dat;
        return true;
    case AX3292_ADCBAUD:
        *value = s->sfr[spr >> 2];
        return true;

    case AX3292_RTC_CON:
        /*
         * Bits 0/1 are the RTC's event-pending flags (read by the ISR at
         * 0x0202df68); the rest is the driver's control value.
         */
        *value = s->rtc_con;
        return true;
    case AX3292_RTC_PND:
        *value = 0;
        return true;

    case 0xc828:                    /* DE video/OSD address readbacks */
    case 0xc830:
    case 0xc844:
    case 0xc854:
    case 0xc87c:
        *value = ax3292_de_address_register(s->sfr[spr >> 2]);
        return true;

    case AX3292_LCD_STA:
        /* TODO(approximation): ready is forced high. Hardware preview samples
         * include both 00980000 and 80980000; command/frame timing is unmodelled. */
        *value = s->sfr[spr >> 2] | 0x80000000 | s->lcd_events;
        return true;

    case AX3292_DAC_OSTAUS:
        *value = s->dac_sta;
        return true;

    case 0x989c:
        /* SD command argument on write, card response on read. */
        *value = s->sd_response;
        return true;

    case AX3292_ENG_STA:
        *value = s->sfr[spr >> 2] | s->eng_sta;
        return true;
    case 0x9900:
    case 0x9904:
    case 0x9908:
        *value = ax3292_tt_read(&s->tt, spr, qemu_clock_get_ns(s->tick_clock));
        return true;
    case AX3292_DAC_ADR:
        /* bit 31 is the busy bit the driver spins on until it clears. */
        *value = s->sfr[spr >> 2] & ~0x80000000u;
        return true;

    case AX3292_USB_CSR0:
        *value = s->sfr[spr >> 2] | AX3292_FRAME_DONE;
        return true;
    case AX3292_ROT_STA:
        *value = s->sfr[spr >> 2];
        return true;

    case AX3292_LCD_B_CTRL:
        *value = s->sfr[spr >> 2];
        return true;

    case AX3292_PIC_PR:
        *value = s->sfr[spr >> 2];
        return true;
    case AX3292_PIC_MR:
        *value = env->picmr;
        return true;
    case AX3292_PIC_SR:
        *value = env->picsr;
        return true;
    case AX3292_PIC_VR:
        if (s->watch != 0 && s->dump_count < 4 &&
            (s->dump_count > 0 || s->tick_count > 150)) {
            unsigned n = s->watch_words ? s->watch_words : 8;
            unsigned i;
            s->dump_count++;
            fprintf(stderr, "ax3292: watch %08x (pc %08x)\n",
                    (uint32_t)s->watch, (uint32_t)env->pc);
            for (i = 0; i < n; i++) {
                uint32_t w = 0;
                if (s->watch + i * 4 + 4 <= AX3292_RAM_SIZE) {
                    memcpy(&w, s->ram_ptr + s->watch + i * 4, sizeof(w));
                }
                fprintf(stderr, "  +%02x = %08x%s", i * 4, w,
                        (i % 4 == 3) ? "\n" : "");
            }
        }
        /*
         * The SDK reads the pending vector out of PICVR: bits 16..20 for the
         * high-priority path and bits 0..4 for the low-priority one
         * (`switch((mfspr(SPR_PICVR) >> 16) & 0x1f)` and `& 0x1f`).  Give both
         * the number of the highest-priority enabled pending interrupt.
         */
        {
            uint32_t pending = env->picsr & env->picmr;
            uint32_t priority = s->sfr[AX3292_PIC_PR >> 2];
            uint32_t hp = pending & priority;
            uint32_t lp = pending & ~priority;
            *value = ((hp ? ctz32(hp) : 0) << 16) |
                     (lp ? ctz32(lp) : 0);
        }
        return true;
    case AX3292_PIC_CR:
        *value = s->sfr[spr >> 2];
        return true;

    case AX3292_TMR0_CON:
    case AX3292_TMR0_PR:
        *value = s->sfr[spr >> 2];
        return true;
    case AX3292_TMR0_CNT:
        *value = 0;
        return true;
    case AX3292_TMR0_PND:
        *value = s->tmr_pnd;
        return true;

    default:
        break;
    }

    if (spr >= 0x8000 && (spr >> 2) < AX3292_SFR_COUNT) {
        *value = s->sfr[spr >> 2];
        if (spr == 0x8000 && s->printer.directory && (s->sfr[0x8040 >> 2] & 0x80)) {
            /* Input pin samples the cover/paper switch, not its output latch. */
            *value = (*value & ~0x80u) | (s->printer.paper ? 0 : 0x80);
        }
        return true;
    }
    return false;
}

static bool ax3292_reg_write(Ax3292State *s, CPUOpenRISCState *env,
                             uint32_t spr, uint32_t v)
{
    switch (spr) {
    case 0x1800:                       /* DCCR */
    case 0x2000:                       /* ICCR */
        s->sfr[spr >> 2] = v;
        return true;
    case AX3292_SPR_SR:
        cpu_set_sr(env, v);
        return true;
    case AX3292_SPR_PPC:
        env->ppc = v;
        return true;
    case AX3292_SPR_EPCR:
        env->epcr = v;
        return true;
    case AX3292_SPR_EEAR:
        env->eear = v;
        return true;
    case AX3292_SPR_ESR:
        env->esr = v;
        return true;
    case AX3292_SPR_EVBA:
        env->evbar = v;
        return true;

    case AX3292_UART0_CON:
        s->uart_con = v;
        return true;
    case AX3292_UART0_BAUD:
        s->uart_baud = v;
        return true;
    case AX3292_UART0_DAT:
        ax3292_uart_tx(v & 0xff);
        s->uart_pnd |= AX3292_UART_PND_TX_READY;
        if (s->watch != 0 && s->watch_count < 40) {
            s->watch_count++;
            fprintf(stderr, "uart byte %02x r9=%08x r1=%08x pc=%08x\n",
                    (unsigned)(v & 0xff),
                    cpu_get_gpr(env, 9), cpu_get_gpr(env, 1),
                    (uint32_t)env->pc);
        }
        return true;
    case AX3292_UART0_PND:
        s->uart_pnd &= ~v;      /* write 1 to clear */
        return true;

    case AX3292_SPI_GCON:
        /*
         * Bit 14 is chip select (the vendor SDK's ax32xx_spi0CS0Config).
         * The byte-mode helpers rewrite this register before every byte, so
         * the command state machine may only restart on a CS *edge*, not on
         * every write.
         */
        if ((v ^ s->spi_gcon) & 0x4000) {
            ax3292_spi_reset_xfer(s);
        }
        s->spi_gcon = v;
        return true;
    case AX3292_SPI_DCON:
        s->spi_dcon = v;
        return true;
    case AX3292_SPI_BAUD:
        s->spi_baud = v;
        return true;
    case AX3292_SPI_DBUF:
        s->spi_dat = v & 0xff;
        return true;
    case AX3292_SPI_STA:
        s->spi_sta &= ~v;
        return true;
    case AX3292_SPI_DCNT:
        s->spi_cnt = v;
        return true;
    case AX3292_SPI_BADR:
        s->spi_badr = v;
        return true;
    case AX3292_SPI_SADR:
        s->spi_sadr = v;
        return true;

    case AX3292_SPI_ACT:
        if ((v & 0x1) && ax3292_spi_enabled(s)) {
            s->spi_dat = ax3292_spi_transfer(s, s->spi_dat & 0xff);
            s->spi_sta |= 0x1;
        }
        if ((v & 0x2) && ax3292_spi_enabled(s)) {
            ax3292_spi_dma(s);
        }
        if (v & (1u << 17)) {
            s->spi_sta &= ~0x2;
        }
        return true;

    case AX3292_TMR0_CON:
        s->sfr[spr >> 2] = v;
        /*
         * Latch the tick on the first enable.  Gating it on the live bit
         * stopped the clock (the driver clears it around a poll), but
         * free-running from reset moved the first interrupt into the early
         * boot, before the firmware is ready for it.
         */
        if ((v & 0x1) && !s->tick_running) {
            ax3292_tick_set(s, true);
        }
        return true;
    case AX3292_TMR0_PR:
    case AX3292_TMR0_CNT:
        s->sfr[spr >> 2] = v;
        return true;
    case AX3292_TMR0_PND:
        s->tmr_pnd &= ~v;       /* write 1 to clear (ISR does |= BIT(5)) */
        /*
         * The timer's pending flag drives its PIC input, so clearing it must
         * deassert the interrupt -- otherwise the CPU re-takes IRQ0 the moment
         * the ISR returns (a storm), because nothing else ever writes PICSR.
         */
        if (!(s->tmr_pnd & AX3292_TMR_PND_BIT)) {
            env->picsr &= ~(1u << s->tick_irq);
            ax3292_irq_update(s);
        }
        return true;

    case AX3292_PIC_PR:
        s->sfr[spr >> 2] = v;
        ax3292_irq_update(s);
        return true;
    case 0x90a0:
    case 0x90dc:
        ax3292_i2c_command(s, spr == 0x90dc, v);
        return true;
    case 0xb00c:
        s->sfr[spr >> 2] &= ~v;
        if (!(s->sfr[0xb00c >> 2] & s->sfr[0xb014 >> 2] & 0x7fffffff)) {
            env->picsr &= ~(1u << 14);
            ax3292_irq_update(s);
        }
        return true;

    case AX3292_PIC_MR:
        if (s->force_irq0) {
            v |= 1u << AX3292_IRQ_TIMER0;   /* diagnostic: keep the timer0 tick alive */
        }
        env->picmr = v;
        ax3292_irq_update(s);
        return true;
    case AX3292_PIC_SR:
        env->picsr &= ~v;
        ax3292_irq_update(s);
        return true;
    case AX3292_PIC_VR:
    case AX3292_PIC_CR:
        s->sfr[spr >> 2] = v;
        return true;

    case 0x9900:
    case 0x9904:
    case 0x9908:
        ax3292_tt_write(&s->tt, spr, v, qemu_clock_get_ns(s->tick_clock));
        return true;

    case AX3292_ADCBAUD:
        s->sfr[spr >> 2] = v;
        return true;
    case AX3292_ADCCON:
    {
        /*
         * The driver sets EN|channel, then ORs in GO and polls EOC.  Convert
         * immediately and publish the result in ADCDAT.
         */
        s->adc_con = v;
        if (v & AX3292_ADC_GO) {
            uint32_t ch = v & 0xf;
            uint32_t val = 0;

            if (ch == s->key_channel) {
                val = s->key_value;
                if (s->key_file != NULL) {
                    FILE *f = fopen(s->key_file, "r");
                    if (f != NULL) {
                        unsigned long k;
                        if (fscanf(f, "%lu", &k) == 1) {
                            s->key_value = k & 0x3ff;
                        }
                        fclose(f);
                    }
                }
                val = s->key_value;
                if (s->key_trace && val != 1023) {
                    fprintf(stderr, "adc-key: channel=%u value=%u\n", ch, val);
                }
            } else {
                val = ax3292_adc_sample(ch, s->key_channel, s->key_value);
                /* Battery divider 700 and bandgap 280 at nominal 800 mV
                 * correspond to 2 * 800 * 700 / 280 = 4000 mV. */
            }
            s->adc_dat = val;
            /*
             * AX3292_ADC_EOC (0x40) is a *busy* bit, not a "done" bit: the
             * SDK's ax32xx_adcRead (0x0202f85c) spins while it is set and only
             * then reads ADCDAT.  Setting it here made every keypad read stale,
             * which is why button presses never reached the UI.  A conversion
             * completes immediately in the model. Hardware also clears GO;
             * see the stock-HAL conversion measurement in docs/hw/2026-10-15.md.
             */
            s->adc_con = ax3292_adc_completed_control(v);
        }
        return true;
    }

    case AX3292_DAC_OCTRL:
        s->sfr[spr >> 2] = v;
        s->dac_seen = true;
        return true;

    case AX3292_ENG_STA:
        /* SDCON0 has read-only status bits; acknowledgements use SDCON1. */
        s->sfr[spr >> 2] = v & ~0x7f8000u;
        return true;

    case AX3292_ENG_CTRL:
        ax3292_sd_control(s, v);
        return true;

    case AX3292_DAC_OSYNC:
        /* Write 1 to bit N to clear output-buffer status bit N. */
        s->dac_sta &= ~(v & 0x7u);
        s->sfr[spr >> 2] = v;
        if (!(s->dac_sta & 0x7u)) {
            CPUOpenRISCState *e = env;
            e->picsr &= ~(1u << AX3292_IRQ_DAC);
            ax3292_irq_update(s);
        }
        return true;

    case AX3292_ROT_STA:
        /* the guest starts/acks the engine through this register */
        s->sfr[spr >> 2] = v;
        /* ack the completion interrupt */
        {
            CPUOpenRISCState *e = env;
            e->picsr &= ~(1u << AX3292_IRQ_ROTATE);
            ax3292_irq_update(s);
        }
        return true;

    case 0x8900:                    /* MCP0CON */
    case 0x8910:                    /* MCP1CON */
    case 0x8920:                    /* MCP2CON */
        /* Enable bit 0; status bit 1; write bit 3 acknowledges completion. */
        s->sfr[spr >> 2] = (v & ~0xau) |
            ((v & 8) ? 0 : (s->sfr[spr >> 2] & 2));
        return true;

    case 0x890c:                    /* MCP0LENGTH: writing length starts DMA */
    case 0x891c:
    case 0x892c:
    {
        uint32_t base = spr - 12;
        uint32_t src = s->sfr[(base + 4) >> 2];
        uint32_t dst = s->sfr[(base + 8) >> 2];
        uint64_t len = (uint64_t)v + 1;
        s->sfr[spr >> 2] = v;
        if ((s->sfr[base >> 2] & 1) && len <= AX3292_RAM_SIZE &&
            (uint64_t)src + len <= AX3292_RAM_SIZE &&
            (uint64_t)dst + len <= AX3292_RAM_SIZE) {
            memmove(s->ram_ptr + dst, s->ram_ptr + src, len);
            s->sfr[base >> 2] |= 2;
        }
        return true;
    }

    case 0x898C:                    /* ROTATECON: bit 31 = start / busy */
        s->sfr[spr >> 2] = v;
        if (v & 0x80000000u) {
            /*
             * An earlier reading called this "the display enable" and set
             * lcd_enabled from it; it is really the rotate engine's control
             * register.  Both uses are kept: the frame interrupt is still only
             * raised once the firmware is driving the display pipeline, and the
             * busy bit now clears so the completion poll at 0x02032cec
             * succeeds.
             */
            s->lcd_enabled = true;
            /*
             * hal_rotateKick (0x02044378) polls this bit ten times *with no
             * delay between attempts* and spins forever if it is still set, so
             * the engine has to read as idle.  Leaving it set for even one tick
             * (10 ms) is enough to hang the UI permanently.
             */
            s->sfr[spr >> 2] = v & ~0x80000000u;
            s->sfr[AX3292_ROT_STA >> 2] = 1;
            /*
             * The rotate engine chains its own pipeline: hal_rotateFrameDoneIsr
             * (0x02044428) frees the finished frame, hands the display its
             * frame, and starts the next composition.  It runs from the rotate
             * completion interrupt (IRQ 23), which ax32xx_rotateStart enables
             * via ax32xx_intEnable(0x17, int_en).  Without raising it the
             * pipeline stops after the boot frame and the composed destination
             * never updates -- which is exactly what the static display was.
             */
            s->rot_irq_pending = 3;

            /*
             * Perform the plane copy the hardware would.  Without it the
             * composed frame never reaches the destination (panel) buffer and
             * the screen keeps showing whatever was there before -- which is
             * exactly what the application looked like it was doing.
             *
             * Register semantics from the SDK prototype of ax32xx_rotateStart:
             *   ROTIMGSIZE  = ((h & ~1) - 1) << 16 | ((w & ~1) - 1)
             *   ROTADDRSTRI = (dst_stride >> 2) << 16 | (src_stride >> 2)
             * both strides in bytes, both planes copied.
             */
            {
                uint32_t sz   = s->sfr[0x89a0 >> 2];
                uint32_t strd = s->sfr[0x89a4 >> 2];
                uint32_t w    = (sz & 0xffff) + 1;
                uint32_t h    = ((sz >> 16) & 0xffff) + 1;
                uint32_t dstr = ((strd >> 16) & 0xffff) * 4;
                uint32_t sstr = (strd & 0xffff) * 4;
                const uint32_t pairs[2][2] = {
                    { AX3292_ROT_SRCY, AX3292_ROT_TGTY },
                    { AX3292_ROT_SRCUV, AX3292_ROT_TGTUV },
                };
                int pi;

                if (w > 1024) { w = 1024; }
                if (h > 1024) { h = 1024; }
                if (dstr == 0) { dstr = w; }
                if (sstr == 0) { sstr = w; }
                for (pi = 0; pi < 2; pi++) {
                    uint32_t src = s->sfr[pairs[pi][0] >> 2];
                    uint32_t dst = s->sfr[pairs[pi][1] >> 2];
                    unsigned unit = pi ? 2 : 1;
                    unsigned pw = pi ? w / 2 : w;
                    unsigned ph = pi ? h / 2 : h;
                    /* Fixed 90-degree counterclockwise rotation. UV is interleaved
                     * 4:2:0: rotate chroma pairs, not individual bytes. */
                    if (src == 0 || dst == 0 ||
                        (uint64_t)src + (ph - 1) * sstr + pw * unit > AX3292_RAM_SIZE ||
                        (uint64_t)dst + (pw - 1) * dstr + ph * unit > AX3292_RAM_SIZE) {
                        continue;
                    }
                    for (unsigned sy = 0; sy < ph; sy++) {
                        for (unsigned sx = 0; sx < pw; sx++) {
                            memcpy(s->ram_ptr + dst + (pw - sx - 1) * dstr + sy * unit,
                                   s->ram_ptr + src + sy * sstr + sx * unit, unit);
                        }
                    }
                }
            }
        }
        return true;

    case 0xb800:                    /* JPGA encoder run/stop */
    {
        uint32_t previous = s->sfr[spr >> 2];
        s->sfr[spr >> 2] = v & ~0x200u;
        if (v & 0x200) {
            s->sfr[spr >> 2] &= ~0x100u;
            s->jpeg_a_pending = false;
        } else if (!(v & 0x100)) {
            s->jpeg_a_pending = false;
        } else if (!(previous & 0x100)) {
            s->jpeg_a_pending = true;
        }
        return true;
    }
    case 0xb810:                    /* output DMA base, reset load pointer */
        s->sfr[spr >> 2] = v;
        s->sfr[0xb818 >> 2] = s->sfr[0xb81c >> 2] = v;
        return true;
    case 0xb83c:                    /* JPGA interrupt mask/global enable */
        s->sfr[spr >> 2] = v;
        ax3292_jpeg_a_irq(s);
        return true;
    case 0xb8b0:                    /* JPGA W1C completion/error status */
        s->sfr[0xb840 >> 2] &= ~v;
        ax3292_jpeg_a_irq(s);
        return true;

    case 0xc000:
        s->sfr[spr >> 2] = v;
        s->jpeg_b_pending = (v & 0x20000040u) == 0x20000040u && !(v & 0x80);
        if (s->jpeg_b_pending) {
            s->sfr[0xc040 >> 2] &= ~0x44u; /* new DMA is not already complete */
            ax3292_jpeg_b_irq(s);
            /* Sample this DMA's registers/data now; publish completion later.
             * UI producers may program the next image before the timer tick. */
            s->jpeg_b_ok = ax3292_jpeg_decode(s);
            s->jpeg_b_ready_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000;
        }
        return true;
    case 0xc03c:
        s->sfr[spr >> 2] = v;
        ax3292_jpeg_b_irq(s);
        return true;
    case 0xc0b0:
        s->sfr[0xc040 >> 2] &= ~v;
        ax3292_jpeg_b_irq(s);
        return true;

    case 0xc828:                    /* hardware keeps 24-bit SDRAM offsets */
    case 0xc830:
    case 0xc844:
    case 0xc854:
        s->sfr[spr >> 2] = ax3292_de_address_register(v);
        return true;

    case 0xc800:                    /* DE_FRAME_CTRL update handshake */
        s->sfr[spr >> 2] = v & ~2u;
        s->sfr[0xc87c >> 2] = s->sfr[0xc828 >> 2];
        return true;

    case 0xa800:                    /* LCDCTRL */
        /*
         * Bit 1 is the frame kick that ax32xx_lcdKick() pulses (0xa800 |= 2);
         * real hardware self-clears it.  Latching it made the firmware think a
         * frame was permanently in progress, which is why raising the true
         * frame event (bit 8 of 0xa804) derailed the boot.
         */
        s->sfr[spr >> 2] = v & ~0x2u;
        s->lcd_seen = true;
        return true;

    case AX3292_LCD_STA:
    {
        /* Write 1 to bit N+8 to clear event bit N. */
        uint32_t clr = 0;
        int b;
        for (b = 8; b < 32; b++) {
            if (v & (1u << b)) {
                clr |= 1u << (b - 8);
            }
        }
        s->lcd_events &= ~clr;
        /* Status and W1C acknowledgement fields are not configuration. */
        s->sfr[spr >> 2] = v & 0x7fff0000u;
        if (!(s->lcd_events & AX3292_LCD_EV_FRAME)) {
            CPUOpenRISCState *e = env;
            e->picsr &= ~(1u << AX3292_IRQ_LCDC);
            ax3292_irq_update(s);
        }
        return true;
    }

    case AX3292_RTC_CON:
        /* Keep the hardware event flags, store the rest. */
        s->rtc_con = (v & ~AX3292_RTC_EVENTS) | (s->rtc_con & AX3292_RTC_EVENTS);
        return true;
    case AX3292_RTC_PND:
        /* Write 1 to clear; clearing the source deasserts the PIC input. */
        s->rtc_con &= ~v;
        if (!(s->rtc_con & AX3292_RTC_EVENTS)) {
            env->picsr &= ~(1u << s->tick_irq);
            ax3292_irq_update(s);
        }
        return true;

    default:
        break;
    }

    if (spr >= 0x8000 && (spr >> 2) < AX3292_SFR_COUNT) {
        uint32_t old = s->sfr[spr >> 2];
        s->sfr[spr >> 2] = v;
        if (spr == 0x8000 || spr == 0x8004 || spr == 0x800c || spr == 0x8010) {
            ax_printer_gpio(&s->printer, spr, old, v, s->sfr);
        }
        return true;
    }
    return false;
}

/* --- machine -------------------------------------------------------------- */

/* There is exactly one AX3292 machine per process. */
static Ax3292State *ax3292_current;

/*
 * Periodic PC sample: a stuck firmware loop may touch no registers at all,
 * which is when this is the only cheap way to find it.  AX3292_PC_SAMPLE=1.
 */
static void ax3292_sample(void *opaque)
{
    OpenRISCCPU *cpu = opaque;
    Ax3292State *s = ax3292_current;

    if (s != NULL) {
        if (s->trace) {
            fprintf(stderr, "pc %08x ppc %08x spr_ops %lu",
                    (uint32_t)cpu->env.pc, (uint32_t)cpu->env.ppc,
                    s->trace_count);
            for (int i = 1; i <= 6; i++) {
                fprintf(stderr, " r%d=%08x", i, cpu_get_gpr(&cpu->env, i));
            }
            fprintf(stderr, " r9=%08x", cpu_get_gpr(&cpu->env, 9));
            fprintf(stderr, " sr=%08x picmr=%08x picsr=%08x pnd=%08x",
                    cpu->env.sr, cpu->env.picmr, cpu->env.picsr,
                    ax3292_current->tmr_pnd);
            fputc('\n', stderr);
        }
        if (s->sampler != NULL) {
            timer_mod(s->sampler,
                      qemu_clock_get_ms(s->tick_clock) + s->sample_ms);
        }
    }
}

static void ax3292_cpu_reset(void *opaque)
{
    OpenRISCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    uint32_t entry = ax3292_current ? ax3292_current->entry : AX3292_ENTRY;

    cpu_reset(cs);
    cpu_set_pc(cs, entry);
    cpu_set_gpr(&cpu->env, 1, ax3292_current ? ax3292_current->stack_top : AX3292_STACK_TOP);
    /* The firmware runs with EVBAR 0 and SR[EPH] selecting the 0xf0000000 alias. */
    cpu->env.evbar = 0;
}

static void ax3292_machine_init(MachineState *machine)
{
    Ax3292State *s = AX3292_MACHINE(machine);
    OpenRISCCPU *cpu;
    const char *env;
    ssize_t size;

    s->trace = false;
    s->trace_limit = 300000;
    s->trace_max = 0xffff;
    static const unsigned default_keys[] = {
        657, 510, 252, 776, 386, 12, 386, 1024, 1024, 1024, 1024
    };
    memcpy(s->key_adc, default_keys, sizeof(default_keys));
    s->tick_ms = AX3292_TICK_MS_DEFAULT;
    s->tick_irq = AX3292_IRQ_TIMER0;
    ax3292_current = s;

    /* Board wiring and boot handoff arrive as machine properties, so a run is
     * described by its command line.  Diagnostics are separate and inert unless
     * AX3292_DEBUG is set. */
    ax3292_debug = getenv("AX3292_DEBUG") != NULL &&
                   strcmp(getenv("AX3292_DEBUG"), "0") != 0;

    if (s->panel_rotation != 90 && s->panel_rotation != 270) {
        error_report("panel-rotation must be 90 or 270");
        exit(1);
    }
    s->panel_ccw = s->panel_rotation == 270;
    if (s->key_channel > 15) {
        error_report("key-channel must be 0..15");
        exit(1);
    }
    if (s->vbg_addr > 0xfffc || (s->vbg_addr & 3)) {
        error_report("vbg-addr must be an aligned SRAM word");
        exit(1);
    }
    if (s->key_values != NULL) {
        ax3292_parse_adc_list(s->key_values, s->key_adc, 6, 1023, "key-values");
    }
    if (s->extra_keys != NULL) {
        ax3292_parse_adc_list(s->extra_keys, s->key_adc + 6, 5, 1024, "extra-keys");
    }
    s->key_value &= 0x3ff;
    /* Original KC02 JEDEC read twice on hardware, 2026-10-15. */
    s->flash_id[0] = 0x5e; s->flash_id[1] = 0x40; s->flash_id[2] = 0x16;
    if (s->flash_id_str != NULL) {
        ax3292_hex_identity(s->flash_id_str, s->flash_id, 3, "flash-id");
    }
    if (s->flash_uid_str != NULL) {
        ax3292_hex_identity(s->flash_uid_str, s->flash_uid, 8, "flash-uid");
    }

    s->fb_every = 5;
    env = ax3292_dgetenv("AX3292_DUMP_FB");
    if (env != NULL) {
        s->fb_path = env;
        s->fb_addr = 0x02104080;
        s->fb_every = 50;
        env = ax3292_dgetenv("AX3292_DUMP_FB_EVERY");
        if (env != NULL) {
            s->fb_every = MAX(5, strtoul(env, NULL, 0));
        }
    }
    env = ax3292_dgetenv("AX3292_DEBUG_TICK_MS");
    if (env != NULL) {
        s->tick_ms = strtoul(env, NULL, 0);
    }
    s->lcdc_irq = true;     /* the display's frame interrupt; IRQ4 */
    s->pulse_irq = -1;
    s->pulse_period = 10;
    s->rot_irq = true;
    s->jpeg_trace = ax3292_dgetenv("AX3292_TRACE_JPEG") != NULL;
    static const unsigned phases[] = {10,8,9,1,5,4,6,2};
    s->printer.directory = s->printer_directory;
    s->printer.trace = ax3292_dgetenv("AX3292_TRACE_PRINTER") != NULL;
    memcpy(s->printer.phases, phases, sizeof(phases));
    if (s->printer_phases_str != NULL) {
        /* Board wiring: each board connects the four platen-motor phase lines to
         * different SFR bits, so the order in which the firmware's waveforms map
         * onto the phase vector differs. */
        const char *q = s->printer_phases_str;
        unsigned n = 0;

        while (n < 8) {
            char *end;
            unsigned long v = strtoul(q, &end, 10);

            if (end == q || v > 15) {
                error_report("printer-phases needs eight values 0..15");
                exit(1);
            }
            s->printer.phases[n++] = v;
            if (*end != ':') { break; }
            q = end + 1;
        }
        if (n != 8) {
            error_report("printer-phases needs eight values 0..15");
            exit(1);
        }
    }
    s->printer.paper = !s->printer_no_paper;
    if (s->printer.directory &&
        g_mkdir_with_parents(s->printer.directory, 0755)) {
        error_report("Cannot create printer output directory: %s", strerror(errno));
        exit(1);
    }
    s->sd_trace = ax3292_dgetenv("AX3292_TRACE_SD") != NULL;
    if (s->sd_path != NULL) {
        ax3292_sd_open(s, s->sd_path);
    }
    env = ax3292_dgetenv("AX3292_DEBUG_PULSE_IRQ");
    if (env != NULL) {
        s->pulse_irq = atoi(env);
    }
    env = ax3292_dgetenv("AX3292_DEBUG_PULSE_PERIOD");
    if (env != NULL && atoi(env) > 1) {
        s->pulse_period = atoi(env);
    }

    env = ax3292_dgetenv("AX3292_TRACE_PROBE");
    if (env != NULL) {
        const char *q = env;

        while (*q && s->probe_n < 8) {
            char *end;
            unsigned long a = strtoul(q, &end, 16);

            if (end == q) {
                break;
            }
            s->probe[s->probe_n] = (uint32_t)a;
            s->probe_prev[s->probe_n] = 0xffffffffu;
            s->probe_n++;
            q = (*end == ',') ? end + 1 : end;
        }
        s->probe_tick = 0;
    }

    env = ax3292_dgetenv("AX3292_DEBUG_LCDC_IRQ");
    if (env != NULL) {
        s->lcdc_irq = atoi(env) != 0;
    }
    env = ax3292_dgetenv("AX3292_DEBUG_FORCE_IRQ0");
    if (env != NULL) {
        s->force_irq0 = atoi(env) != 0;
    }
    env = ax3292_dgetenv("AX3292_DEBUG_TICK_IRQ");
    if (env != NULL) {
        s->tick_irq = strtoul(env, NULL, 0);
    }
    env = ax3292_dgetenv("AX3292_TRACE_WATCH");
    if (env != NULL) {
        s->watch = strtoull(env, NULL, 0);
    }
    env = ax3292_dgetenv("AX3292_TRACE_WATCH_WORDS");
    if (env != NULL) {
        s->watch_words = strtoul(env, NULL, 0);
    }
    s->key_trace = ax3292_dgetenv("AX3292_TRACE_KEY") != NULL;
    env = ax3292_dgetenv("AX3292_TRACE_SPR");
    if (env != NULL) {
        s->trace = atoi(env) != 0;
    }
    env = ax3292_dgetenv("AX3292_TRACE_SPR_MIN");
    if (env) { s->trace_min = strtoul(env, NULL, 0); }
    env = ax3292_dgetenv("AX3292_TRACE_SPR_MAX");
    if (env) { s->trace_max = strtoul(env, NULL, 0); }
    env = ax3292_dgetenv("AX3292_TRACE_SPR_LIMIT");
    if (env != NULL) {
        s->trace_limit = strtoul(env, NULL, 0);
    }
    env = ax3292_dgetenv("AX3292_TRACE_SPI");
    if (env != NULL) {
        s->spi_trace = atoi(env) != 0;
    }

    /* The flash backing store is the dump itself. */
    if (s->flash_path != NULL) {
        gsize len = 0;

        if (g_file_get_contents(s->flash_path, (gchar **)&s->flash, &len, NULL) &&
            len > 0) {
            s->flash_size = len;
        } else {
            error_report("could not read flash image '%s'", s->flash_path);
            exit(1);
        }
    }

    if (machine->ram_size < AX3292_RAM_SIZE) {
        error_report("ax3292 requires at least 64 MiB RAM");
        exit(1);
    }
    memory_region_init_ram(&s->ram, NULL, "ax3292.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0, &s->ram);
    s->ram_ptr = memory_region_get_ram_ptr(&s->ram);

    /*
     * The low 64 KiB is SRAM and holds the exception vectors.  The core also
     * reaches low SRAM through a high alias at 0xf0000000, so map it there too;
     * the firmware reads back through that alias while booting.
     *
     * This is deliberately *not* upstream OpenRISC SR[EPH] vectoring.  On this
     * part SR bit 14 selects SRAM vectors (the SDK's intvct_sram/intvct_mask)
     * and interrupt entry still uses the low EVBA, which is what X8D's relative
     * ISR jumps require.  See target/or1k/interrupt.c.
     */
    memory_region_init_alias(&s->low_alias, NULL, "ax3292.low",
                             &s->ram, 0, 0x10000);
    memory_region_add_subregion(get_system_memory(), 0xf0000000, &s->low_alias);

    if (s->flash != NULL) {
        /* The vendor code sits at file = VMA + 0x200, so its vector slots land
         * at .org 0x0000 + exc*16 with no bias. */
        size_t src = 0x200;
        size_t n = s->flash_size > src ? s->flash_size - src : 0;

        n = MIN(n, 0x10000);
        memcpy(memory_region_get_ram_ptr(&s->ram), s->flash + src, n);
        /* App-entry handoff: ROM/SDRAM calibration normally initialises the
         * boot driver's vbg_param. Its firmware reader at 0x784 uses 0x222c.
         * We skip ROM bring-up, so supply the SDK's nominal 800 mV bandgap
         * calibration, not an application-state or instruction patch. */
        stl_le_p(s->ram_ptr + s->vbg_addr, 800);
        if (s->trace) {
            fprintf(stderr, "ax3292: low memory <- flash %#zx (%zu bytes)\n", src, n);
        }
    }

    cpu = OPENRISC_CPU(cpu_create(machine->cpu_type));
    s->cpu = cpu;
    cpu->env.nodelay = true;        /* the vendor toolchain emits no delay slots */
    cpu->env.little_endian = true;  /* AX3292 reset SR has LEE set */
    cpu->env.vec_bias = 0;
    cpu->env.spr_hook = ax3292_spr_access;
    cpu->env.spr_hook_opaque = s;
    qemu_register_reset(ax3292_cpu_reset, cpu);

    /*
     * Real time by default, and run with -accel tcg,thread=multi so the main
     * loop keeps running while the vCPU executes.  A virtual-clock tick only
     * fires when the vCPU leaves its TB chain, which a tight guest loop
     * (while (1) delay(50);) never does; making it fire with -icount instead
     * costs a main-loop round trip per instruction (~12 us, measured), which
     * is far too slow to reach the UI.
     */
    s->tick_clock = QEMU_CLOCK_REALTIME;
    env = ax3292_dgetenv("AX3292_DEBUG_CLOCK");
    if (env != NULL && strcmp(env, "virtual") == 0) {
        s->tick_clock = QEMU_CLOCK_VIRTUAL;
    }
    static const GraphicHwOps display_ops = { 0 };
    s->console = qemu_graphic_console_create(NULL, 0, &display_ops, s);
    qemu_console_resize(s->console, 320, 240);
    s->display_bh = qemu_bh_new(ax3292_display_update, s);
    s->input = qemu_input_handler_register(NULL, &ax3292_keyboard);
    qemu_input_handler_activate(s->input);
    s->tick = timer_new_ms(s->tick_clock, ax3292_tick, s);

    env = ax3292_dgetenv("AX3292_TRACE_PC");
    if (env != NULL && atoi(env) != 0) {
        s->sample_ms = 500;
        env = ax3292_dgetenv("AX3292_TRACE_PC_MS");
        if (env != NULL) {
            s->sample_ms = strtoul(env, NULL, 0);
        }
        s->sampler = timer_new_ms(s->tick_clock, ax3292_sample, cpu);
        timer_mod(s->sampler, qemu_clock_get_ms(s->tick_clock) + s->sample_ms);
    }

    if (machine->kernel_filename) {
        size = load_image_targphys(machine->kernel_filename, AX3292_SDRAM_BASE,
                                   machine->ram_size - AX3292_SDRAM_BASE, NULL);
        if (size < 0) {
            error_report("could not load application '%s'",
                         machine->kernel_filename);
            exit(1);
        }
        if (s->trace) {
            fprintf(stderr, "ax3292: loaded %s at %#x (%zd bytes)\n",
                    machine->kernel_filename, AX3292_SDRAM_BASE, size);
        }
    }

    if (s->trace) {
        fprintf(stderr, "ax3292: entry %#x stack %#x flash %zu bytes trace %lu\n",
                s->entry, s->stack_top, s->flash_size, s->trace_limit);
    }
}

/*
 * Machine properties: board wiring and boot handoff.
 *
 * A run is described by its command line, so these live on the machine rather
 * than in the environment:
 *
 *   -M ax3292,flash=original.bin,sd-image=card.img,sensor=on
 *
 * MachineClass derives from ObjectClass, not DeviceClass, so DEFINE_PROP and
 * -global do not apply; the accessors below are offset-based instead.  The
 * defaults come from ax3292_machine_instance_init.  Diagnostics are separate
 * and inert unless AX3292_DEBUG is set (see ax3292_dgetenv).
 */
static void ax3292_prop_get_u32(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    uint32_t *field = (void *)((char *)obj + (uintptr_t)opaque);
    uint32_t value = *field;

    visit_type_uint32(v, name, &value, errp);
}

static void ax3292_prop_set_u32(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    uint32_t *field = (void *)((char *)obj + (uintptr_t)opaque);

    visit_type_uint32(v, name, field, errp);
}

static void ax3292_prop_get_bool(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    bool *field = (void *)((char *)obj + (uintptr_t)opaque);
    bool value = *field;

    visit_type_bool(v, name, &value, errp);
}

static void ax3292_prop_set_bool(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    bool *field = (void *)((char *)obj + (uintptr_t)opaque);

    visit_type_bool(v, name, field, errp);
}

static void ax3292_prop_get_str(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    char **field = (void *)((char *)obj + (uintptr_t)opaque);
    char *value = g_strdup(*field);

    if (value == NULL) {
        return;     /* an unset property has no value at all */
    }
    visit_type_str(v, name, &value, errp);
    g_free(value);
}

static void ax3292_prop_set_str(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    char **field = (void *)((char *)obj + (uintptr_t)opaque);
    char *value = NULL;

    if (!visit_type_str(v, name, &value, errp)) {
        return;
    }
    g_free(*field);
    *field = value;     /* visit_type_str hands over ownership on success */
}

static void ax3292_prop_release_str(Object *obj, const char *name, void *opaque)
{
    char **field = (void *)((char *)obj + (uintptr_t)opaque);

    g_free(*field);
    *field = NULL;
}

#define AX3292_PROP_U32(_name, _field)                                        \
    object_class_property_add(oc, _name, "uint32", ax3292_prop_get_u32,       \
                              ax3292_prop_set_u32, NULL,                      \
                              (void *)(uintptr_t)offsetof(Ax3292State, _field))
#define AX3292_PROP_BOOL(_name, _field)                                       \
    object_class_property_add(oc, _name, "bool", ax3292_prop_get_bool,        \
                              ax3292_prop_set_bool, NULL,                     \
                              (void *)(uintptr_t)offsetof(Ax3292State, _field))
#define AX3292_PROP_STR(_name, _field)                                        \
    object_class_property_add(oc, _name, "string", ax3292_prop_get_str,       \
                              ax3292_prop_set_str, ax3292_prop_release_str,   \
                              (void *)(uintptr_t)offsetof(Ax3292State, _field))

static void ax3292_machine_instance_init(Object *obj)
{
    Ax3292State *s = AX3292_MACHINE(obj);

    s->entry = AX3292_ENTRY;
    s->stack_top = AX3292_STACK_TOP;
    s->vbg_addr = 0x222c;
    s->panel_rotation = 90;
    s->key_channel = AX3292_ADC_KEY_CH;
    s->key_value = 0x3ff;    /* no key pressed */
}

static void ax3292_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "AX3292 camera (KC02 family)";
    mc->init = ax3292_machine_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = OPENRISC_CPU_TYPE_NAME("or1200");
    mc->default_ram_size = AX3292_RAM_SIZE;

    AX3292_PROP_U32("entry", entry);
    AX3292_PROP_U32("stack-top", stack_top);
    AX3292_PROP_U32("vbg-addr", vbg_addr);
    AX3292_PROP_U32("panel-rotation", panel_rotation);
    AX3292_PROP_U32("key-channel", key_channel);
    AX3292_PROP_U32("key-value", key_value);
    AX3292_PROP_STR("key-values", key_values);
    AX3292_PROP_STR("extra-keys", extra_keys);
    AX3292_PROP_STR("key-file", key_file);
    AX3292_PROP_BOOL("sensor", sensor);
    AX3292_PROP_BOOL("sensor-3a", sensor_3a);
    AX3292_PROP_BOOL("dual-sensor", dual_sensor);
    AX3292_PROP_STR("flash", flash_path);
    AX3292_PROP_STR("flash-id", flash_id_str);
    AX3292_PROP_STR("flash-uid", flash_uid_str);
    AX3292_PROP_STR("sd-image", sd_path);
    AX3292_PROP_STR("printer-dir", printer_directory);
    AX3292_PROP_STR("printer-phases", printer_phases_str);
    AX3292_PROP_BOOL("printer-no-paper", printer_no_paper);
}

static const TypeInfo ax3292_machine_typeinfo = {
    .name          = TYPE_AX3292_MACHINE,
    .parent        = TYPE_MACHINE,
    .class_init    = ax3292_machine_class_init,
    .instance_init = ax3292_machine_instance_init,
    .instance_size = sizeof(Ax3292State),
};

static void ax3292_machine_register_types(void)
{
    type_register_static(&ax3292_machine_typeinfo);
}

type_init(ax3292_machine_register_types)
