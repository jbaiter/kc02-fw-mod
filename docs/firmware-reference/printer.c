/* Stock KC02 firmware reference: the thermal printer.
 *
 * Decompiled from firmware/original.bin (Ghidra, analysis artifact
 * 218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5). The SDK has
 * no printer application code, so this whole file is decompilation.
 *
 * THE TWO SHARED BLOCKS. The printer is not a standalone peripheral; it borrows
 * hardware the camera pipeline also owns:
 *
 *   1. THE SENSOR IIC PINS. The head is bit-banged through PORTE.15 (data) and
 *      PORTE.13 (clock) -- exactly the software-IIC SDA/SCL lines. Every job
 *      begins by driving those two pins low to release them (kc02_print_run_A/_B
 *      steps 2-3), and the sensor cannot be read while a job runs.
 *
 *   2. THE MJPEG-B BLOCK. Print-time image scaling goes through
 *      ax32xx_mjpB_Ctl_init / ax32xx_mjpB_dma_cfg, and ax32xx_recfg_mjpb_tminf
 *      exists to switch the 0xc050..0xc074 register block between the two users.
 *
 * So "print while capturing" is not a scheduling problem, it is a hardware
 * conflict. A port has to sequence both switches explicitly.
 *
 * Nothing here has been exercised on hardware. Luma's print_photo slot is NULL on
 * purpose (see SAFETY at the bottom).
 *
 * PROVENANCE NOTE: where a body below is written out, it is a faithful reading of
 * the decompile. Where it is summarised, the body was NOT transcribed and the
 * comment says so -- do not assume a summary is executable.
 */
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* ===========================================================================
 * GPIO map. Worth stating explicitly because the vendor headers skip a port:
 * ax329x.h defines PORTA, PORTB, PORTD, PORTE, PORTF, PORTG, PORTH, PORTJ --
 * THERE IS NO PORTC. The registers run 0x8000, 0x8004, 0x8008, 0x800c, ...
 *
 *   0x8000 PORTA   0x8004 PORTB   0x8008 PORTD   0x800c PORTE
 *   0x8010 PORTF   0x8014 PORTG   0x8018 PORTH   0x801c PORTJ
 *
 * hal_gpio*(ch,...) indexes that sequence, so ch 3 = PORTE -- consistent with the
 * sensor pins living on ch 3 (PE13/PE15). The ordinal is authoritative; the older
 * SDK doc comment ("PA,PB,PC,PD,PE,...") misletters the fourth port.
 *
 * Lines used by the print path:
 *   PORTB.6  0x40    head supply enable (also the settle pulse)
 *   PORTB.5  0x20    head LATCH
 *   PORTA.9  0x200   head STROBE
 *   PORTA.7  0x80    paper/cover sensor (input)
 *   PORTE.15 0x8000  head DATA   <-- shared with sensor SDA
 *   PORTE.13 0x2000  head CLOCK  <-- shared with sensor SCL
 *   PORTE.0  0x1     platen coil A phase
 *   PORTE.1  0x2     platen coil A phase
 *   PORTF.10 0x400   platen coil B phase
 *   PORTF.11 0x800   platen coil B phase
 *   PORTA.3  0x8     platen common
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 * Function map.
 *
 *   0x020591cc  kc02_delay                  busy loop, arg*500 iterations    [body]
 *   0x02059208  kc02_busy_delay             busy loop, arg*20 iterations     [body]
 *   0x02059248  kc02_strobe_pulse           PORTA.9 high, delay, low         [body]
 *   0x02059290  kc02_battery_check          ADC ch1 -> supply/energy code    [body]
 *   0x020593a4  kc02_sensor_read            PORTA.7 paper/cover sensor       [body]
 *   0x020593cc  kc02_load_print_image       JPEG decode + scale to head size [summary]
 *   0x0205980c  kc02_sfr4800_save_restore   save/restore 13 interrupt enables[table]
 *   0x02059c80  kc02_printer_helper_C80     head supply on/off + motor off   [body]
 *   0x02059d14  kc02_motor_step             one platen step (4 phases)       [body]
 *   0x02059f84  kc02_printer_init           pin directions + settle          [body]
 *   0x0205a124  kc02_head_strobe_delay      battery/energy -> strobe time    [body]
 *   0x0205a1fc  kc02_print_head_ramp        the head driver                  [body]
 *   0x0205a6c4  kc02_print_run_A            job: checks + ramp + feed        [body]
 *   0x0205a87c  kc02_print_run_B            job: checks + inline 4x overprint[body]
 *   0x0205ab70  kc02_print_layout           paper canvas selection           [summary]
 *   0x0205adf0  kc02_print_file             print from card        [NOT READ YET]
 *   0x0205b0fc  kc02_print_image            entry point of the print API     [body]
 *   0x0202d234  ax32xx_recfg_mjpb_tminf     MJPEG-B <-> printer switch       [body]
 *   0x0200a4bc  ui_task_print_image         UI task                [NOT READ YET]
 *   0x0200c24c  ui_task_print_file          UI task                [NOT READ YET]
 * ------------------------------------------------------------------------ */

/* ---------------------------------------------------------------------------
 * Timing primitives. Pure busy loops calibrated in ITERATIONS, not milliseconds --
 * a port must re-derive them for its own clock.
 * ------------------------------------------------------------------------ */
void kc02_delay(int n)      { for (int i = n * 500; i; --i) {} }
void kc02_busy_delay(int n) { for (int i = n * 20;  i; --i) {} }

/* ---------------------------------------------------------------------------
 * 0x02059248  kc02_strobe_pulse(hold)
 * PORTA.9 high, busy_delay(hold), low. `hold` arrives in r3.
 * ------------------------------------------------------------------------ */
void kc02_strobe_pulse(int hold)
{
    PORTA |= 0x200;
    kc02_busy_delay(hold);
    PORTA &= ~0x200;
}

/* ---------------------------------------------------------------------------
 * 0x020593a4  kc02_sensor_read() -> hal_gpioRead(0, 0x80)
 * PORTA.7. Returns 0 when paper is present; both job functions abort otherwise.
 * ------------------------------------------------------------------------ */
int kc02_sensor_read(void) { return hal_gpioRead(0, 0x80); }

/* ---------------------------------------------------------------------------
 * 0x02059f84  kc02_printer_init()
 *
 * Pin setup plus a power-on settle. PORTB.6 is pulsed: low, high, settle, low.
 * The rest are driven to their idle levels, PORTA.7 is made an input.
 *
 * Note this runs on EVERY kc02_print_image call, so the settle is paid per job,
 * not once at boot.
 * ------------------------------------------------------------------------ */
void kc02_printer_init(void)
{
    hal_gpioInit(1, 0x40, 0, 0);  hal_gpioWrite(1, 0x40, 0);
    hal_gpioInit(1, 0x40, 0, 0);  hal_gpioWrite(1, 0x40, 1);
    kc02_busy_delay(500);
    hal_gpioInit(1, 0x40, 0, 0);  hal_gpioWrite(1, 0x40, 0);
    hal_gpioInit(3, 0x1,   0, 0);  hal_gpioWrite(3, 0x1,   0);
    hal_gpioInit(4, 0x400, 0, 0); hal_gpioWrite(4, 0x400, 0);
    hal_gpioInit(0, 0x8,   0, 0); hal_gpioWrite(0, 0x8,   0);
    hal_gpioInit(4, 0x800, 0, 0); hal_gpioWrite(4, 0x800, 0);
    hal_gpioInit(0, 0x80,  1, 0);                       /* PORTA.7 input */
    hal_gpioInit(1, 0x20,  0, 0); hal_gpioWrite(1, 0x20, 1);
    hal_gpioInit(0, 0x200, 0, 0); hal_gpioWrite(0, 0x200, 0);
    hal_gpioInit(0, 0x200, 0, 0); hal_gpioWrite(0, 0x200, 0);
}

/* ---------------------------------------------------------------------------
 * 0x02059c80  kc02_printer_helper_C80(on)
 *
 * De-energises every motor coil first, then sets the head supply. Turning the
 * supply ON costs a kc02_delay(0x14) settle; OFF is immediate. The asymmetry is
 * deliberate -- the supply must be stable before the first strobe.
 * ------------------------------------------------------------------------ */
void kc02_printer_helper_C80(int on)
{
    hal_gpioWrite(3, 0x1,   0);   /* PORTE.0  coil A off */
    hal_gpioWrite(4, 0x400, 0);   /* PORTF.10 coil B off */
    hal_gpioWrite(0, 0x8,   0);   /* PORTA.3  common   off */
    hal_gpioWrite(4, 0x800, 0);   /* PORTF.11 coil B off */
    if (on == 0) {
        hal_gpioWrite(1, 0x40, 0);
    } else {
        hal_gpioWrite(1, 0x40, 1);
        kc02_delay(0x14);
    }
}

/* ---------------------------------------------------------------------------
 * 0x02059d14  kc02_motor_step()
 *
 * One full platen step = 4 phase changes with kc02_delay(0x32) between the first
 * three; the 4th is left asserted with no delay so consecutive steps chain.
 *
 * There is NO phase table: the two half-cycles are written out explicitly and
 * selected by printer_state.motor_phase (byte 0x020D2ED9), toggled at the end of
 * every call.
 *
 *   set A (motor_phase != 0)               set B (motor_phase == 0)
 *   1 PE&=~1 PF|=0x400 PA&=~8 PF|=0x800     1 PE|=1 PF&=~0x400 PA|=8  PF&=~0x800
 *   2 PE&=~1 PF&=~0x400 PA&=~8 PF|=0x800    2 PE&=~1 PF&=~0x400 PA|=8 PF&=~0x800
 *   3 PE|=1  PF&=~0x400 PA&=~8 PF|=0x800    3 PE&=~1 PF|=0x400  PA|=8 PF&=~0x800
 *   4 PE|=1  PF&=~0x400 PA&=~8 PF&=~0x800   4 PE&=~1 PF|=0x400  PA&=~8 PF&=~0x800
 *   (delay after 1,2,3 only in both sets)
 *
 * These four phases are also what kc02_print_head_ramp asserts at its sub-strobe
 * boundaries -- see that function. The platen therefore advances in lockstep with
 * the strobes during a print; kc02_motor_step is the standalone-feed entry point.
 * ------------------------------------------------------------------------ */
void kc02_motor_step(void)
{
    int a = printer_state.motor_phase != 0;

    if (a) {
        PORTE &= ~0x1;  PORTF |= 0x400;   PORTA &= ~0x8;  PORTF |= 0x800;   kc02_delay(0x32);
        PORTE &= ~0x1;  PORTF &= ~0x400;  PORTA &= ~0x8;  PORTF |= 0x800;   kc02_delay(0x32);
        PORTE |= 0x1;   PORTF &= ~0x400;  PORTA &= ~0x8;  PORTF |= 0x800;   kc02_delay(0x32);
        PORTE |= 0x1;   PORTF &= ~0x400;  PORTA &= ~0x8;  PORTF &= ~0x800;
    } else {
        PORTE |= 0x1;   PORTF &= ~0x400;  PORTA |= 0x8;   PORTF &= ~0x800;  kc02_delay(0x32);
        PORTE &= ~0x1;  PORTF &= ~0x400;  PORTA |= 0x8;   PORTF &= ~0x800;  kc02_delay(0x32);
        PORTE &= ~0x1;  PORTF |= 0x400;   PORTA |= 0x8;   PORTF &= ~0x800;  kc02_delay(0x32);
        PORTE &= ~0x1;  PORTF |= 0x400;   PORTA &= ~0x8;  PORTF &= ~0x800;
    }
    printer_state.motor_phase = !a;
}

/* ---------------------------------------------------------------------------
 * 0x02059290  kc02_battery_check() -> supply/energy code
 *
 * Reads ADC channel 1 and maps it onto a coarse level. Two board flags gate it:
 *
 *   0x020ccdc8  charger present. NON-ZERO skips the ladder entirely; the function
 *               falls through to clear 0x020c8788 and return 0x0e.
 *   0x020c8788  a latch this function SETS, which forces the ADC to 0x14 and so
 *               suppresses the low-battery verdict on later calls.
 *
 * Effective ladder with no charger (0x020ccdc8 == 0):
 *
 *     raw < 450   -> 8    (low; also sets the 0x020c8788 latch)
 *     raw < 500   -> 6
 *     raw < 550   -> 5
 *     raw < 600   -> 4
 *     raw < 650   -> 3
 *     raw < 700   -> 2
 *     raw < 750   -> 1
 *     otherwise   -> 0
 *
 * NOTE THE HOLE: 7 IS NEVER RETURNED. Callers treat 8 as a fault and 0x0e as
 * "charging". head_strobe_delay carries an explicit case for code 7 that is
 * therefore unreachable from here.
 * ------------------------------------------------------------------------ */
int kc02_battery_check(void)
{
    hal_gpioInit(0, 0x40, 1, 1);
    int raw = hal_adcGetChannel(1);
    if (g_020c8788 != 0) { raw = 0x14; }

    if (raw < 0x1c2) {                     /* < 450 */
        if (g_020ccdc8 == 0) { g_020c8788 = 1; return 8; }
    } else if (g_020ccdc8 == 0) {
        if (raw < 500)   return 6;
        if (raw < 0x226) return 5;         /* < 550 */
        if (raw < 600)   return 4;
        if (raw < 0x28a) return 3;         /* < 650 */
        if (raw < 700)   return 2;
        return raw < 0x2ee;                /* < 750 -> 1, else 0 */
    }
    g_020c8788 = 0;
    return 0x0e;
}

/* ---------------------------------------------------------------------------
 * 0x0205a124  kc02_head_strobe_delay(energy, mode) -> strobe time, -1 on fault
 *
 *     strobe = (3 - energy) * -0x30 + 0x120 + mode * -4 - battery_base
 *
 * So more energy -> longer pulse, more mode -> shorter (mode clamped to 4).
 *
 * battery base:  8 -> ABORT (-1)   7 -> 0x18   6 -> 0x14   5 -> 0x10
 *                4 -> 0x0c         3 -> 0x08   2 -> 0x04   else -> 0
 *
 * The 7 -> 0x18 case exists here but kc02_battery_check never returns 7, so the
 * branch looks unreachable through the normal path. Flagged, not deleted.
 * ------------------------------------------------------------------------ */
short kc02_head_strobe_delay(unsigned energy, unsigned mode)
{
    mode &= 0xff;
    signed char bat = kc02_battery_check();
    if (bat == 8) { return -1; }

    short base;
    if      (bat == 7) base = 0x18;
    else if (bat == 6) base = 0x14;
    else if (bat == 5) base = 0x10;
    else if (bat == 4) base = 0x0c;
    else if (bat == 3) base = 0x08;
    else if (bat == 2) base = 0x04;
    else               base = 0;

    if (mode > 4) { mode = 4; }
    return (short)(((3 - (energy & 0xff)) * -0x30 + 0x120 + mode * -4) - base);
}

/* ---------------------------------------------------------------------------
 * 0x0205a1fc  kc02_print_head_ramp(canvas, mode, energy, stride, count, strobe, p6)
 *
 * The head driver, called with SEVEN arguments from kc02_print_run_A; Ghidra
 * infers five, the last two arrive on the stack.
 *
 *   canvas  byte-per-dot image buffer (see BUFFER ORDER)
 *   mode    0 = threshold ramp 0,8,...,248 ; 1 = fixed threshold 0x80
 *   energy  0..3 -> strobe divisor 8,7,6,5 ; anything else 4
 *   stride  ALSO used as the row count
 *   count   dots per line
 *   strobe  base width from kc02_head_strobe_delay (used in the width formula)
 *   p6      passed through, not read
 *
 * BUFFER ORDER -- the trap. The dot loop indexes
 *
 *     canvas[col * stride + row]      col in [0,count)   row in [0,stride)
 *
 * so row varies fastest inside a column of stride `stride`. That is TRANSPOSED
 * relative to a conventional row-major image, which is exactly why
 * kc02_print_layout exists as a separate reordering step. Feed a row-major buffer
 * straight in and the result is rotated/streaked but still "looks like it
 * printed", because the geometry is near-square.
 *
 * DOT TEST (note the asymmetry):  dot ON when pixel <= threshold, OFF when
 * pixel > threshold. Equality turns the dot on.
 *
 * STRUCTURE
 *   1. build the 32-entry threshold ramp (0,8,...,248)
 *   2. battery code 8 -> return 0xff at once
 *   3. battery -> energy bias (values below, derived by simulation)
 *   4. build a 385-entry table row[i] = i / divisor
 *   5. per row: toggle row parity, then 32 sub-strobes. At sub 0,8,16,24 the four
 *      coil phases from kc02_motor_step are asserted and kc02_delay(10) is taken;
 *      every sub clocks `count` dots on PORTE.15, latches PORTB.5, then
 *        width = row[dots_on] + (bias + 10 + energy*8) * 2, clamped to 8000
 *      and calls kc02_strobe_pulse(width).
 *   6. after 32 sub-strobes: sys_irq_clear(), re-read the paper sensor; stop when
 *      paper is gone
 *
 * battery -> bias (exact, by simulation of the short-circuiting source):
 *     code   0    1    2    3    4   5    6    7
 *     bias  12   12   12    8    0   0   -4   -4
 *
 * RETURN  8 = completed (paper ran out), 0xff = battery fault, 0 = row negative
 * ------------------------------------------------------------------------ */
u32 kc02_print_head_ramp(u8 *canvas, signed char mode, u32 energy, u32 stride,
                         int count, int strobe, int p6)
{
    short thr[32];
    u16   rowtab[385];
    short s = 0;
    for (int i = 0; i < 32; i++) { thr[i] = s; s += 8; }

    signed char bat = kc02_battery_check();
    if (bat == 8) { return 0xff; }

    int bias = -4;
    if ((((1 < (u32)(char)bat - 6) && (bias = 0, 1 < (u32)(char)bat - 4)) &&
         (bias = 8, bat != 3)) && ((u32)(char)bat < 3)) {
        bias = 0xc;
    }

    int divisor = 8;
    if      (energy == 1) divisor = 7;
    else if (energy == 2) divisor = 6;
    else if (energy == 3) divisor = 5;
    else if (energy >= 4) divisor = 4;

    for (int i = 0; i < 385; i++) { rowtab[i] = (u16)(i / divisor); }

    int parity = 1;
    int r = (int)stride;
    for (;;) {
        r = (int)((r - 1) * 0x10000) >> 16;
        if (r < 0) { return 0; }
        parity ^= 1;

        for (int sub = 0; sub < 32; sub++) {
            int threshold = (mode == 1) ? 0x80 : thr[sub];

            /* The four coil phases, asserted only at the sub-strobe boundaries.
             * These are verbatim kc02_motor_step's set A (parity 1) and set B
             * (parity 0), minus the kc02_delay(0x32) -- here the delay is 10. */
            switch (sub) {
            case 0:
                if (parity) { PORTE &= ~0x1; PORTF |= 0x400;  PORTA &= ~0x8; PORTF |= 0x800; }
                else        { PORTE |= 0x1;  PORTF &= ~0x400; PORTA |= 0x8;  PORTF &= ~0x800; }
                kc02_delay(10);
                break;
            case 8:
                if (parity) { PORTE &= ~0x1; PORTF &= ~0x400; PORTA &= ~0x8; PORTF |= 0x800; }
                else        { PORTE &= ~0x1; PORTF &= ~0x400; PORTA |= 0x8;  PORTF &= ~0x800; }
                kc02_delay(10);
                break;
            case 16:
                if (parity) { PORTE |= 0x1;  PORTF &= ~0x400; PORTA &= ~0x8; PORTF |= 0x800; }
                else        { PORTE &= ~0x1; PORTF |= 0x400;  PORTA |= 0x8;  PORTF &= ~0x800; }
                kc02_delay(10);
                break;
            case 24:
                if (parity) { PORTE |= 0x1;  PORTF &= ~0x400; PORTA &= ~0x8; PORTF &= ~0x800; }
                else        { PORTE &= ~0x1; PORTF |= 0x400;  PORTA &= ~0x8; PORTF &= ~0x800; }
                kc02_delay(10);
                break;
            default:
                break;   /* sub 1-7, 9-15, 17-23, 25-31: clock dots only */
            }

            int dots_on = 0;
            for (u32 i = stride; i < stride * (u32)count; i += stride) {
                if ((int)(signed char)canvas[i + r] > threshold) {
                    PORTE &= ~0x8000;        /* pixel > threshold -> dot off */
                } else {
                    PORTE |= 0x8000;         /* pixel <= threshold -> dot on  */
                    dots_on++;
                }
                PORTE |= 0x2000;             /* clock high */
                PORTE &= ~0x2000;            /* clock low  */
            }
            PORTB &= ~0x20;                  /* latch      */
            PORTB |= 0x20;

            u32 width = rowtab[dots_on] + (u32)(bias + 10 + (int)energy * 8) * 2;
            if (width > 8000) { width = 8000; }
            kc02_strobe_pulse((int)width);
        }
        sys_irq_clear();
        if (kc02_sensor_read() != 0) { break; }
    }

    return 8;
}

/* ---------------------------------------------------------------------------
 * 0x0205a6c4  kc02_print_run_A(canvas, w, h, energy, mode, p6)
 *
 *   1. PORTB.6 high, busy_delay(0x32)
 *   2. release PORTE.15 and PORTE.13 (the sensor IIC pins), busy_delay(500)
 *   3. paper sensor must read 0, else return 9
 *   4. strobe = head_strobe_delay(energy, p6); negative -> abort
 *   5. head supply on, then head_ramp(...)
 *   6. ramp != 8 and != 0x0e -> feed 40 motor steps, kc02_delay(0x78) between
 *   7. always: supply off, zero the job counter, PORTB.6 low
 * ------------------------------------------------------------------------ */
int kc02_print_run_A(void *canvas, u16 w, u16 h, u8 energy, u8 mode, u8 p6)
{
    hal_gpioInit(0, 0x80, 1, 0);
    hal_gpioWrite(1, 0x40, 1);
    kc02_busy_delay(0x32);
    hal_gpioInit(3, 0x8000, 0, 0);  hal_gpioWrite(3, 0x8000, 0);   /* PE15 */
    hal_gpioInit(3, 0x2000, 0, 0);  hal_gpioWrite(3, 0x2000, 0);   /* PE13 */
    kc02_busy_delay(500);

    int rc = kc02_sensor_read();
    if (rc == 0) {
        rc = 9;
        short strobe = kc02_head_strobe_delay(energy, p6);
        if (strobe < 0) { goto done; }
        kc02_printer_helper_C80(1);
        rc = kc02_print_head_ramp(canvas, mode, energy, w, h, strobe, p6);
        if (rc != 8) {
            if (rc != 0xe) {
                for (short n = 0x28; n != 0; --n) {
                    kc02_motor_step(); kc02_delay(0x78);
                    kc02_motor_step(); kc02_delay(0x78);
                }
            }
            goto done;
        }
    }
    rc = 8;
done:
    kc02_printer_helper_C80(0);
    printer_state.counter = 0;
    hal_gpioWrite(1, 0x40, 0);
    return rc;
}

/* ---------------------------------------------------------------------------
 * 0x0205a87c  kc02_print_run_B(canvas, stride, count, energy, p5)
 *
 * Job B: a second, INLINE driver rather than a call into head_ramp. It prints
 * 1-BIT data (threshold 0x81; the read is signed, so < 0x81 means dot on) and
 * over-prints each line FOUR times -- that is how it gets grey out of a
 * two-state head.
 *
 * RETURN  8 = paper ran out mid-job, 9 = battery fault, 0xe = battery charging,
 *         0x10 = bad geometry, 0 = success
 *
 * Geometry guard is `count == 0x180` (384); anything else returns 0x10 after the
 * supply is dropped.
 *
 * The energy bases differ from head_ramp's and point the OPPOSITE way:
 *
 *     battery code        0    1    2    3   4   5   6   7
 *     run_B base         20   16   12    8   4   4   0   0
 *     head_ramp bias     12   12   12    8   0   0  -4  -4
 *     strobe_delay base   0    0    4    8  12  16  20  24
 *
 * run_B decreases with charge while strobe_delay increases. The reason is not
 * established. Recorded as observed, not explained.
 * ------------------------------------------------------------------------ */
int kc02_print_run_B(void *canvas, u16 stride, short count, u32 energy, signed char p5)
{
    hal_gpioInit(0, 0x80, 1, 0);
    hal_gpioWrite(1, 0x40, 1);
    kc02_busy_delay(0x32);
    hal_gpioInit(3, 0x8000, 0, 0);  hal_gpioWrite(3, 0x8000, 0);
    hal_gpioInit(3, 0x2000, 0, 0);  hal_gpioWrite(3, 0x2000, 0);
    kc02_busy_delay(500);

    if (kc02_sensor_read() != 0) { return 8; }

    int bat = kc02_battery_check();
    if (bat == 8)  { return 9; }
    if (bat == 0xe) { return 0xe; }

    int base = 0;
    if (((1 < (unsigned)(bat - 6)) &&
         (((base = 4, 1 < (unsigned)(bat - 4) && (base = 8, bat != 3)) && (base = 0xc, bat != 2)))) &&
        ((base = 0x10, bat != 1 && bat == 0))) {
        base = 0x14;
    }

    u16 rc = 0x10;
    if (p5 == 1) {
        kc02_printer_helper_C80(1);
        base += ((energy & 0xff) == 0) ? 0x10 : 0xc + (int)(energy & 0xff) * 0xc;
        rc = 0x10;
        if (count != 0x180) { goto done; }

        for (u32 rr = stride; ; ) {
            int r = (int)((rr - 1) * 0x10000) >> 16;
            if (r < 0) { break; }
            rr = (u32)r;
            kc02_motor_step();
            for (short pass = 4; pass != 0; --pass) {
                for (u32 i = stride; i < (u32)stride * 0x180; i += stride) {
                    if ((signed char)((u8 *)canvas)[i + r] < 0x81) {
                        PORTE |= 0x8000;
                    } else {
                        PORTE &= ~0x8000;
                    }
                    PORTE |= 0x2000;
                    PORTE &= ~0x2000;
                }
                PORTB &= ~0x20;
                PORTB |= 0x20;
                kc02_strobe_pulse(base * 0xc + 100);
                sys_irq_clear();
                if (kc02_sensor_read() != 0) { return 8; }
            }
        }
    }

    for (short n = 0x28; n != 0; --n) {
        kc02_motor_step(); kc02_delay(0x78);
        kc02_motor_step(); kc02_delay(0x78);
        rc = 0;
    }
done:
    kc02_printer_helper_C80(0);
    hal_gpioWrite(1, 0x40, 0);
    return rc;
}

/* ---------------------------------------------------------------------------
 * 0x0205ab70  kc02_print_layout(canvas, w, h)   [SUMMARY -- body not transcribed]
 *
 * Picks a paper canvas from the image aspect ratio and letterboxes into it. The
 * canvas set, in the order the code tests it (dots):
 *
 *    384x672   672x384   384x216   216x384
 *    320x180   320x240   512x384
 *
 * The draw call's third size argument is 8 or 10 depending on the branch; 0x40 is
 * fixed. If the draw returns 0 it ends with a fill using 0x7f. FUN_02022978 and
 * FUN_020231e0 are unnamed graphics helpers, not resolved here.
 * ------------------------------------------------------------------------ */

/* ---------------------------------------------------------------------------
 * 0x0205980c  kc02_sfr4800_save_restore(save)   [TABLE -- body not transcribed]
 *
 * Printing runs with interrupts off. This snapshots 13 interrupt-enable bits from
 * SFR 0x4800 on save, disables all 13, and on restore re-enables only those that
 * were set -- so a job can never widen the enabled set.
 *
 *   bit of 0x4800   interrupt   stored in
 *   14  0x4000       0x0e        sfr4800_bits[0xc]
 *   16  0x10000      0x10        sfr4800_bits[0xb]
 *   22  0x400000     0x16        sfr4800_bits[10]
 *    4  0x10         4           sfr4800_bits[9]
 *   21  0x200000     0x15        sfr4800_bits[8]
 *   23  0x800000     0x17        sfr4800_bits[7]
 *   26  0x4000000    0x1a        sfr4800_bits[6]
 *   29  0x20000000   0x1d        sfr4800_bits[5]
 *   15  0x8000       0x0f        sfr4800_bits[4]
 *    0  0x1          0           sfr4800_bits[3]
 *    1  0x2          1           sfr4800_bits[2]
 *    2  0x4          2           sfr4800_bits[1]
 *    3  0x8          3           sfr4800_bits[0]
 *
 * For a RAM-run port this is real: anything depending on those 13 interrupts is
 * suspended for the whole job. head_ramp's per-row sys_irq_clear() is the only
 * servicing that happens.
 * ------------------------------------------------------------------------ */

/* ---------------------------------------------------------------------------
 * 0x020593cc  kc02_load_print_image(jpeg, &buf, &w, &h, len)
 *                                                    [SUMMARY -- body not transcribed]
 *
 * Decodes the JPEG to YUV420 and scales it to a head-sized bitmap. This is where
 * the MJPEG-B sharing becomes concrete:
 *
 *   - waits for the backrec decoder and the CSI scaler to go idle, PIP off
 *   - parses the JPEG header; on -10 it SCANS BACKWARDS for the 0x4a 0x52 0x58
 *     ("JRX") trail and re-parses from there -- an EXIF/thumbnail skip
 *   - sizes to head width 0x180 (384) unless the source is already that size,
 *     preserving aspect with a round-down to a multiple of 0x20
 *   - allocates w*h*3/2 and decodes into it (hal_mjpegDecodePicture_2)
 *   - if scaling is needed: allocates a 0x19000 scratch buffer and drives the
 *     MJPEG-B block -- ax32xx_mjpB_Ctl_init(0, 4, w, h, ...),
 *     ax32xx_mjpB_dma_cfg, hal_watermarkEnable(0,0) -- then decodes again
 *
 * Returns 0 ok, 1 alloc failed, 2 scratch alloc failed, 3 decode failed,
 * 4 wait failed, 5 MJPEG-B path failed.
 * ------------------------------------------------------------------------ */

/* ---------------------------------------------------------------------------
 * 0x0202d234  ax32xx_recfg_mjpb_tminf()
 *
 * The camera/printer switch for the MJPEG-B register block: ten registers in
 * 0xc050..0xc074 restored from saved copies, i.e. whichever user ran last saved
 * its configuration and this puts the other one back.
 *
 * Write order as written: 0xc06c 0xc070 0xc074 0xc060 0xc064 0xc068 0xc054
 * 0xc058 0xc05c 0xc050 (descending, then ascending). Reproduced faithfully.
 * ------------------------------------------------------------------------ */
void ax32xx_recfg_mjpb_tminf(void)
{
    REG(0xc06c) = g_saved_020ce17c;  REG(0xc070) = g_saved_020ce180;
    REG(0xc074) = g_saved_020ce184;  REG(0xc060) = g_saved_020ce170;
    REG(0xc064) = g_saved_020ce174;  REG(0xc068) = g_saved_020ce178;
    REG(0xc054) = g_saved_020ce164;  REG(0xc058) = g_saved_020ce168;
    REG(0xc05c) = g_saved_020ce16c;  REG(0xc050) = g_saved_020ce160;
}

/* ---------------------------------------------------------------------------
 * 0x0205b0fc  kc02_print_image(jpeg, energy, use_layout, p4, len)
 *                                                    [SUMMARY -- body not transcribed]
 *
 * The entry point, and the canonical ordering the rest of the file must obey:
 *
 *   kc02_printer_init()
 *   if 0x020ccdc8 != 0 -> clear 0x020c8794
 *   if 0x020c8794 != 0 -> BUSY: open window 2 and return 1, no print at all.
 *                         That is the re-entrancy guard.
 *   kc02_load_print_image(...)
 *   if use_layout -> kc02_print_layout(...)
 *   kc02_sfr4800_save_restore(0)      interrupts OFF
 *   run_B if use_layout else run_A
 *   kc02_sfr4800_save_restore(1)      interrupts restored
 *   hal_sysMemFree(decoded buffer)
 *   PORTA.7 back to input, driven high
 *   ui_state_apply(0)
 * ------------------------------------------------------------------------ */

/* ===========================================================================
 * SAFETY
 *
 * These routines energise a thermal head. The vendor code has no supply
 * interlock, no temperature limit and no head-energy cap -- the board is expected
 * to provide them. Specifically:
 *
 *   - run_A/_B abort on battery fault (code 8) but there is NO duty-cycle check;
 *     back-to-back jobs at high energy are unthrottled beyond the feed delay
 *   - head_ramp's 8000 clamp on strobe width is the ONLY upper bound in the path
 *   - the paper sensor is polled per row, so an empty roll stops the job, but a
 *     JAMMED roll does not -- the sensor cannot tell the two apart
 *
 * custom-firmware/boards/ax3292.c wires the print_photo slot of `Platform` to
 * NULL until interlocks exist. Reading this reference is not a reason to change
 * that.
 * ======================================================================== */
