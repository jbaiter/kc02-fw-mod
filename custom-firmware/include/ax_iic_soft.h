/* AX3292 software (bit-banged) IIC for the image sensor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Transcribed from the stock firmware, not guessed. The stock sensor path does
 * NOT use the hardware IIC0/IIC1 controllers:
 *
 *   sensor_iic_start     0x0202ec74 -> software_iic_start   0x0202ea98
 *   sensor_iic_stop      0x0202ec94 -> software_iic_stop    0x0202eb10
 *   sensor_iic_check_ack 0x0202ecb4 -> software_iic_wait_ack 0x0202eb88
 *   software_iic_init    0x0202ea68
 *   software_iic_sdaout  0x0202e908   software_iic_sda_set 0x0202e970
 *   software_iic_scl_set 0x0202e99c   software_iic_sda_get 0x0202ea40
 *   software_iic_delay_us 0x0202e850
 *
 * This build takes the SDK's USE_SOFTWARE_IIC0 path, so the earlier attempts to
 * drive the sensor through the hardware IIC0 controller (and IIC1) were talking
 * to pins nothing was listening to. That is why both buses failed for our driver
 * AND for the stock hardware-IIC helper, which also read 0xff.
 *
 * Pins, straight from software_iic_scl_set / software_iic_sda_set:
 *   SCL = GPIO port 3 (PE) pin 0x2000 = PE13
 *   SDA = GPIO port 3 (PE) pin 0x8000 = PE15
 * These are the same pins as hardware IIC0_POS2, but software_iic_gpioInit maps
 * them with MAP = 0, i.e. plain GPIO, and drives the protocol by hand.
 *
 * GPIO port 3 register addresses follow the SDK's GPIO_xxx(a) macros with
 * SPRGROUP_SFR0 = 0x8000.
 *
 * Override AX_READ / AX_WRITE to host this against a model.
 */
#ifndef AX_IIC_SOFT_H
#define AX_IIC_SOFT_H

#include "ax_sensor.h"      /* ax_u32 / ax_u8 and the shared SPI helpers */

#ifndef AX_SOFT_READ
#define AX_SOFT_READ(n) AX_SENSOR_READ(n)
#endif
#ifndef AX_SOFT_WRITE
#define AX_SOFT_WRITE(n, v) AX_SENSOR_WRITE(n, v)
#endif

/* GPIO port 3 (PE). */
#define AX_GPIO3_DATA  0x800cu
#define AX_GPIO3_DIR   0x804cu
#define AX_GPIO3_PLU   0x808cu
#define AX_GPIO3_MAP   0x810cu
#define AX_GPIO3_DRV   0x814cu
#define AX_GPIO3_DGL   0x838cu

#define AX_SOFT_SCL    0x2000u   /* PE13 */
#define AX_SOFT_SDA    0x8000u   /* PE15 */

/* software_iic_delay_us(N) is a plain counted loop; the stock uses 0xc around
 * edges and 3 for the acknowledge clock. */
#ifndef AX_SOFT_DELAY_SCALE
#define AX_SOFT_DELAY_SCALE 8u
#endif
static inline void ax_soft_delay(unsigned loops)
{
    unsigned n = loops * AX_SOFT_DELAY_SCALE;
    while (n--) { __asm__ volatile ("l.nop"); }
}

/* software_iic_gpioInit(pins, dir): digital on, MAP = 0 (GPIO function), normal
 * drive, direction, pull-up. MAP = 0 is the part that matters: it is what makes
 * these pins GPIOs rather than the hardware IIC0 function. */
static inline void ax_soft_pin_dir(ax_u32 pin, ax_u32 out)
{
    AX_SOFT_WRITE(AX_GPIO3_DGL, AX_SOFT_READ(AX_GPIO3_DGL) | pin);
    AX_SOFT_WRITE(AX_GPIO3_MAP, AX_SOFT_READ(AX_GPIO3_MAP) & ~pin);
    AX_SOFT_WRITE(AX_GPIO3_DRV, AX_SOFT_READ(AX_GPIO3_DRV) & ~pin);
    if (out) { AX_SOFT_WRITE(AX_GPIO3_DIR, AX_SOFT_READ(AX_GPIO3_DIR) & ~pin); }
    else     { AX_SOFT_WRITE(AX_GPIO3_DIR, AX_SOFT_READ(AX_GPIO3_DIR) |  pin); }
    AX_SOFT_WRITE(AX_GPIO3_PLU, AX_SOFT_READ(AX_GPIO3_PLU) | pin);
}

/* software_iic_sdaout(): both lines driven. */
static inline void ax_soft_sdaout(void)
{
    ax_soft_pin_dir(AX_SOFT_SDA, 1);
    ax_soft_pin_dir(AX_SOFT_SCL, 1);
}

/* software_iic_sdain(): SDA released so the sensor can drive the acknowledge. */
static inline void ax_soft_sdain(void)
{
    ax_soft_pin_dir(AX_SOFT_SDA, 0);
}

static inline void ax_soft_scl(ax_u32 v)
{
    if (v) { AX_SOFT_WRITE(AX_GPIO3_DATA, AX_SOFT_READ(AX_GPIO3_DATA) | AX_SOFT_SCL); }
    else   { AX_SOFT_WRITE(AX_GPIO3_DATA, AX_SOFT_READ(AX_GPIO3_DATA) & ~AX_SOFT_SCL); }
}

static inline void ax_soft_sda(ax_u32 v)
{
    if (v) { AX_SOFT_WRITE(AX_GPIO3_DATA, AX_SOFT_READ(AX_GPIO3_DATA) | AX_SOFT_SDA); }
    else   { AX_SOFT_WRITE(AX_GPIO3_DATA, AX_SOFT_READ(AX_GPIO3_DATA) & ~AX_SOFT_SDA); }
}

static inline ax_u32 ax_soft_sda_get(void)
{
    return (AX_SOFT_READ(AX_GPIO3_DATA) & AX_SOFT_SDA) ? 1u : 0u;
}

/* SR (SPR 0x18) bit 1 is the interrupt-enable bit. The stock bit-bang clears it
 * for the whole transfer and sets it again afterwards, so a transfer cannot be
 * preempted mid-clock. Save and restore the caller's value rather than assuming. */
static inline ax_u32 ax_soft_irq_off(void)
{
    ax_u32 sr = AX_SOFT_READ(0x18u);
    AX_SOFT_WRITE(0x18u, sr & ~2u);
    return sr;
}
static inline void ax_soft_irq_restore(ax_u32 sr)
{
    AX_SOFT_WRITE(0x18u, sr);
}

static inline void ax_soft_start(void)
{
    ax_u32 sr = ax_soft_irq_off();
    ax_soft_sdaout();
    ax_soft_sda(1);
    ax_soft_scl(1);
    ax_soft_delay(12);
    ax_soft_sda(0);
    ax_soft_delay(12);
    ax_soft_scl(0);
    ax_soft_irq_restore(sr);
}

static inline void ax_soft_stop(void)
{
    ax_u32 sr = ax_soft_irq_off();
    ax_soft_sdaout();
    ax_soft_scl(0);
    ax_soft_sda(0);
    ax_soft_delay(12);
    ax_soft_scl(1);
    ax_soft_sda(1);
    ax_soft_delay(12);
    ax_soft_irq_restore(sr);
}

/* Returns 1 when the device acknowledged. */
static inline int ax_soft_wait_ack(void)
{
    ax_u32 sr = ax_soft_irq_off();
    int tries = 5;
    ax_soft_sdain();
    ax_soft_sda(1);
    ax_soft_delay(3);
    ax_soft_scl(1);
    ax_soft_delay(3);
    while (tries--) {
        if (ax_soft_sda_get() == 0) {
            ax_soft_scl(0);
            ax_soft_irq_restore(sr);
            return 1;
        }
    }
    ax_soft_stop();
    ax_soft_irq_restore(sr);
    return 0;
}

/* MSB first, data valid while SCL is high, matching the stock byte loop. */
static inline void ax_soft_send_byte(ax_u8 byte)
{
    unsigned bit;
    ax_u32 sr = ax_soft_irq_off();
    ax_soft_sdaout();
    for (bit = 0; bit < 8; ++bit) {
        ax_soft_sda((byte & 0x80u) ? 1u : 0u);
        ax_soft_delay(3);
        ax_soft_scl(1);
        ax_soft_delay(3);
        ax_soft_scl(0);
        ax_soft_delay(3);
        byte = (ax_u8)(byte << 1);
    }
    ax_soft_irq_restore(sr);
}

static inline ax_u8 ax_soft_recv_byte(void)
{
    ax_u8 byte = 0;
    unsigned bit;
    ax_u32 sr = ax_soft_irq_off();
    ax_soft_sdain();
    for (bit = 0; bit < 8; ++bit) {
        ax_soft_delay(3);
        ax_soft_scl(1);
        ax_soft_delay(3);
        byte = (ax_u8)((byte << 1) | (ax_soft_sda_get() & 1u));
        ax_soft_scl(0);
        ax_soft_delay(3);
    }
    ax_soft_irq_restore(sr);
    return byte;
}

/* software_iic_init(): idle both lines high. */
static inline void ax_soft_init(void)
{
    ax_soft_sdaout();
    ax_soft_sda(1);
    ax_soft_scl(1);
}

/* ---- 8-bit register access, as the stock sensor_iic_* layer uses -------------- */

static inline int ax_soft_read_reg(ax_u8 slave, ax_u8 reg, ax_u8 *out)
{
    ax_soft_start();
    ax_soft_send_byte(slave);
    if (!ax_soft_wait_ack()) { ax_soft_stop(); return 0; }
    ax_soft_send_byte(reg);
    if (!ax_soft_wait_ack()) { ax_soft_stop(); return 0; }
    ax_soft_stop();

    ax_soft_start();
    ax_soft_send_byte((ax_u8)(slave | 1u));
    if (!ax_soft_wait_ack()) { ax_soft_stop(); return 0; }
    *out = ax_soft_recv_byte();
    ax_soft_stop();          /* master leaves the acknowledge to the stop */
    return 1;
}

static inline int ax_soft_write_reg(ax_u8 slave, ax_u8 reg, ax_u8 value)
{
    ax_soft_start();
    ax_soft_send_byte(slave);
    if (!ax_soft_wait_ack()) { ax_soft_stop(); return 0; }
    ax_soft_send_byte(reg);
    if (!ax_soft_wait_ack()) { ax_soft_stop(); return 0; }
    ax_soft_send_byte(value);
    if (!ax_soft_wait_ack()) { ax_soft_stop(); return 0; }
    ax_soft_stop();
    return 1;
}

#endif /* AX_IIC_SOFT_H */
