/* Stock KC02 firmware reference: the image-sensor bus and bring-up.
 *
 * Decompiled from firmware/original.bin; function addresses kept per body so each
 * can be re-checked against the binary. See README.md for provenance.
 *
 * THE CENTRAL FACT: this build uses the SDK's USE_SOFTWARE_IIC0 path, so the
 * sensor bus is GPIO bit-banging, not the hardware IIC0/IIC1 controllers.
 * sensor_iic_start/stop/check_ack are thin wrappers over software_iic_*, and
 * software_iic_gpioInit maps the pins with MAP = 0, i.e. plain GPIO.
 *
 *   SCL = GPIO port 3 (PE) pin 0x2000 = PE13        VERIFIED on hardware
 *   SDA = GPIO port 3 (PE) pin 0x8000 = PE15        VERIFIED on hardware
 *
 * Those are the same pins as hardware IIC0_POS2, which is exactly why driving the
 * IIC0 controller appeared to "almost work" and never acknowledged anything.
 *
 * The 8-bit slave address is 0x80 (7-bit 0x40).                    VERIFIED
 */
#include <stdint.h>

typedef uint8_t  u8;
typedef uint32_t u32;

#define REG(n) (*(volatile u32 *)(n))

/* ---------------------------------------------------------------------------
 * GPIO helpers (port 3 = PE). Register addresses come from the SDK's GPIO_xxx()
 * macros with SPRGROUP_SFR0 = 0x8000.
 * ------------------------------------------------------------------------ */

/* 0x0202e884  software_iic_gpioInit_constprop_0(pins, dir)
 * digital on, MAP = 0 (GPIO function), normal drive, direction, pull-up.
 * MAP = 0 is the part that matters: it selects GPIO rather than the IIC0
 * peripheral on those pins. */
void software_iic_gpioInit(u32 pins, u8 dir)
{
    REG(0x838c) |= pins;                 /* DGL   digital enable        */
    REG(0x810c) &= ~pins;                /* MAP   0 = GPIO              */
    REG(0x814c) &= ~pins;                /* DRV   normal drive          */
    if (dir) { REG(0x804c) &= ~pins; }   /* DIR   0 = output            */
    else     { REG(0x804c) |=  pins; }
    REG(0x808c) |= pins;                 /* PLU   pull-up               */
}

/* 0x0202e908  software_iic_sdaout(): both lines driven. */
void software_iic_sdaout(void)
{
    software_iic_gpioInit(0x8000, 0);
    software_iic_gpioInit(0x2000, 0);
}

/* software_iic_sdain(): not decompiled directly; the bit-bang releases SDA so the
 * device can drive the acknowledge. Equivalent to gpioInit(0x8000, 1). */

/* 0x0202e970  software_iic_sda_set(v) */
void software_iic_sda_set(u8 v) { if (v) { REG(0x800c) |= 0x8000; } else { REG(0x800c) &= ~0x8000; } }

/* 0x0202e99c  software_iic_scl_set(v) */
void software_iic_scl_set(u8 v) { if (v) { REG(0x800c) |= 0x2000; } else { REG(0x800c) &= ~0x2000; } }

/* 0x0202ea40  software_iic_sda_get() */
u32  software_iic_sda_get(void) { return (REG(0x800c) & 0x8000) ? 1 : 0; }

/* 0x0202e850  software_iic_delay_us(n): a plain counted loop. */
void software_iic_delay_us(int n) { while (n--) { __asm__ volatile ("l.nop"); } }

/* ---------------------------------------------------------------------------
 * Protocol. Each transfer masks interrupts (SR bit 1) for its whole duration.
 * SR is SPR 0x18.                                                   VERIFIED
 * ------------------------------------------------------------------------ */

#define SR() REG(0x18)

/* 0x0202ea98  software_iic_start() */
void software_iic_start(void)
{
    u32 sr = SR();
    SR() = sr & ~2u;                       /* interrupts off for the transfer */
    software_iic_sdaout();
    software_iic_sda_set(1);
    software_iic_scl_set(1);
    software_iic_delay_us(0xc);
    software_iic_sda_set(0);
    software_iic_delay_us(0xc);
    software_iic_scl_set(0);
    SR() = sr;                             /* restore the caller's value */
}

/* 0x0202eb10  software_iic_stop() */
void software_iic_stop(void)
{
    u32 sr = SR();
    SR() = sr & ~2u;
    software_iic_sdaout();
    software_iic_scl_set(0);
    software_iic_sda_set(0);
    software_iic_delay_us(0xc);
    software_iic_scl_set(1);
    software_iic_sda_set(1);
    software_iic_delay_us(0xc);
    SR() = sr;
}

/* 0x0202eb88  software_iic_wait_ack()
 * Returns 0 when the device acknowledged, 1 when it did not. Note the inverted
 * sense relative to most drivers, and note that it calls stop() on NACK. */
u32 software_iic_wait_ack(void)
{
    u32 sr = SR();
    int tries = -5;                        /* five attempts */
    SR() = sr & ~2u;
    /* sdain() */
    software_iic_sda_set(1);
    software_iic_delay_us(3);
    software_iic_scl_set(1);
    software_iic_delay_us(3);
    do {
        if (software_iic_sda_get() == 0) {
            software_iic_scl_set(0);
            SR() = sr;
            return 0;                      /* acknowledged */
        }
        ++tries;
    } while (tries != 0);
    software_iic_stop();
    SR() = sr;
    return 1;                              /* no acknowledge */
}

/* 0x0202ea68  software_iic_init(): idle both lines high. */
void software_iic_init(void)
{
    software_iic_sdaout();
    software_iic_sda_set(1);
    software_iic_scl_set(1);
}

/* ---------------------------------------------------------------------------
 * sensor_iic_* wrappers (0x0202ec74 / 0x0202ec94 / 0x0202ecb4).
 * These are what prove the bus is software-driven: each one is a single tail
 * call into software_iic_*, with no hardware IIC register access at all.
 * ------------------------------------------------------------------------ */
void sensor_iic_start(void)     { software_iic_start(); }
void sensor_iic_stop(void)      { software_iic_stop(); }
u32  sensor_iic_check_ack(void){ return (software_iic_wait_ack() - 1u) >> 31; }

/* ---------------------------------------------------------------------------
 * Autodetect (0x02058170, Ghidra: sensor_autocheck)
 *
 * Reads a resource header from flash, walks the item array probing each sensor's
 * id register, and on a match copies the item to 0x020e4ba0 and then loads the
 * sensor's ops struct to 0x020e32a0 and its init table to 0x020d1ea0.
 *
 * The table for H63P is 100 {reg,val} pairs at flash 0xd2fb0 -- resource base
 * 0xcd9b0 plus the item's offset field 0x5600 -- and the slice is byte-identical
 * to the RAM copy at 0x020d1ea0 in the baseline dump. It ends with 0xffff 0xffff,
 * which is the terminator the write loop tests for.                  VERIFIED
 *
 * Returns the ops pointer, or the internal test-pattern fallback 0x02083cb0.
 */
#define RESOURCE_BASE 0xcd9b0u
static int *g_item;                        /* piRam020d2ea8 */

u32 sensor_autocheck(void)
{
    /* NB: the buffer the table lands in is a static (0x020d1ea0), NOT the caller's
     * argument -- an earlier probe passed its own buffer and saw it untouched. */
    u8 items[0x50 * 16];

    if (g_item == 0) { g_item = (int *)hal_sysMemMalloc(0x40, 0x40); }
    hal_spiFlashRead(RESOURCE_BASE, (u32)g_item, 0x40);
    u32 total = g_item[1];
    u32 step  = g_item[2];                 /* 0x50 */
    hal_spiFlashRead(g_item[0] + RESOURCE_BASE, (u32)items, total);

    for (u32 i = 0; i < total / step; ++i) {
        /* item[3] = address width, item[6] = id register; two 8-bit fields are
         * byte-swapped when the address width is 2. */
        if (items[i*0x50 + 3] == 2) { /* swap the two id bytes */ }
        u32 id = sensor_read_reg(&items[i*0x50 + 6]);
        if (id == items[i*0x50]) {          /* match */
            memcpy((void *)0x020e4ba0, items + i*0x50, 0x50);
            /* ... then load ops and init table */
            return 0x020e32a0;              /* VERIFIED: real H63P selected */
        }
    }
    return 0x02083cb0;                      /* internal test pattern */
}

/* ---------------------------------------------------------------------------
 * sensor_init (0x020583f0) -- the bring-up, in order.
 *
 * NOTE what is NOT here: it never calls ax32xx_csiInit, so the CSI module clock
 * stays gated (see power_clock_gpio.c). It also does not establish continuous
 * capture; that lives in the camera-entry path.
 * ------------------------------------------------------------------------ */
void sensor_init(void)
{
    if (g_flag_020d1e81 == 0) {
        ax32xx_sysLDOSet(SYS_LDO_LSEN, 6, 1);          /* DVDD rail on */
    } else {
        /* power-cycle the rail */
        ax32xx_sysLDOSet(SYS_LDO_LSEN, 6, 0);
        ax32xx_sysLDOSet(SYS_LDO_LSEN, 6, 1);
        ax32xx_sysCpuMsDelay(0x32);
        ax32xx_sysLDOSet(SYS_LDO_LSEN, 6, 0);
        ax32xx_sysCpuMsDelay(100);
        ax32xx_sysLDOSet(SYS_LDO_LSEN, 6, 1);
        ax32xx_sysCpuMsDelay(0x32);
    }
    csi_set_mclk(16000000);                            /* 16 MHz for the id check */
    hal_iic0Init();                                    /* which ends in a software-IIC stop */
    if (g_sensor_ops == 0) { g_sensor_ops = (void *)sensor_autocheck(); }
    if (g_sensor_ops != 0) {
        ax32xx_sysLDOSet(SYS_LDO_HSEN, h63p.VDDIO, 1);  /* AVDD rail on, sel 16 */
        ax32xx_sysCpuMsDelay(1);
    }
    csi_set_mclk(h63p.mclk);                            /* 24 MHz, from the ops struct */

    /* Walk the table at 0x020d1ea0, writing each entry, until 0xffff 0xffff. */
    for (int i = 0;; i += addr_num + data_num) {
        if (*(u16 *)(0x020d1ea0 + i) == 0xffff) { break; }
        sensor_write_reg((void *)(0x020d1ea0 + i));
        if (i == 0) { ax32xx_sysCpuMsDelay(10); }
    }
    hal_csiRegister(g_sensor_ops, g_sensor_init);
    sensor_frame_rate_adapt(0, h63p.hvb_adapt.fps);
}

/* ---------------------------------------------------------------------------
 * hal_csiRegister (0x020381c8). Trivial: it stores the pointers and sets a flag.
 * It does NOT configure the CSI, which is why it was never the missing call that
 * the capture problem looked like it should be.                      VERIFIED
 * ------------------------------------------------------------------------ */
void hal_csiRegister(void *ops, void *init)
{
    g_csi_ops = ops;
    g_csi_init = init;
    g_csi_registered = 1;
}

/* ---------------------------------------------------------------------------
 * sensor_h62_exp_gain_wr (0x02058b78) -- H63P uses the H62-style writer, reached
 * through the ops struct's fp_exp_gain_wr at ops+0xc70.
 *
 * Encoding, with the constants at 0x02084980 / 0x02084984:
 *   exposure is 16-bit, written as reg 0x02 = exp>>8 and reg 0x01 = exp & 0xff
 *   gain is banded against the thresholds 0x100,0x200,0x400,0x800,0x1000,0x2000,
 *   0x4001, with code ((gain<<8)/threshold - 0x100)>>4 | (band<<4)
 * ------------------------------------------------------------------------ */
extern const u8 exp_template[4];       /* 02 00 01 00 */
extern const u32 gain_table[7];

void sensor_h62_exp_gain_wr(u32 exp, u32 gain)
{
    u8 buf[4];
    memcpy(buf, exp_template, 4);
    buf[3] = (u8)exp;
    buf[1] = (u8)(exp >> 8);
    sensor_write_reg(buf);             /* reg 0x02 = exp high */
    sensor_write_reg(buf + 2);         /* reg 0x01 = exp low  */

    u8 gbuf[2] = { 0, 0 };
    const u32 *band = gain_table;
    int i;
    for (i = 0; i < 6 && (gain < band[0] || band[1] <= gain); ++i) { ++band; }
    if (i == 6) { sensor_write_reg(gbuf); return; }
    gbuf[1] = (u8)((char)(((gain << 8) / band[0] - 0x100) >> 4) | (char)(i << 4));
    sensor_write_reg(gbuf);
}

/* ---------------------------------------------------------------------------
 * Ops struct layout facts established by scanning the struct in RAM for runs of
 * code pointers. The only run of three consecutive ones is the adaptor table at
 * the very end:
 *      ops+0xc68 -> 0x02058c54   fp_rotate
 *      ops+0xc6c -> 0x02058b64   fp_hvblank
 *      ops+0xc70 -> 0x02058b78   fp_exp_gain_wr
 *                       ([ops+0xc70](exp, gain) programs exposure and gain)
 * Measured H63P fields: typ=1 (YUV), mclk=24 MHz, pixelw=1280, pixelh=720,
 * AVDD=16, DVDD=6, VDDIO=16, csi_tun=11, colrarray=2.
 * ------------------------------------------------------------------------ */
typedef void (*fp_exp_gain_wr_t)(u32 exp, u32 gain);
#define OPS_FP_EXP_GAIN_WR(ops) (*(fp_exp_gain_wr_t *)((u8 *)(ops) + 0xc70))
