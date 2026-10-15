/* AX3292 SCCB / IIC sensor-bus primitives.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Shared by the Luma board HAL and the bench probes so there is one
 * implementation of the bus protocol, not two that can drift.
 *
 * Register layout and bit protocol are transcribed from the SDK:
 *   ax32xx/src/ax32xx_iic.c   (IIC0/IIC1 live in SPRGROUP_SFR2, base 0x9000)
 *   hal/src/hal_iic.c         (hal_iic08bitAddrReadData / WriteData)
 *
 * Command bits are write-only in the model: the command register reads back
 * zero, which is why the SDK can use `CMD |= bit` for a multi-step transfer.
 *
 * Bring-up that is board-specific and deliberately NOT done here: the sensor
 * LDO, CSI MCLK and the sensor power/reset lines. See ax_cam_power.h for those.
 * The imager must be powered before it will acknowledge at all; with the imager
 * unpowered the bus stays silent rather than returning a value.
 *
 * Every function takes an explicit bus base so one probe can exercise IIC0 and
 * IIC1 without a second copy of the protocol.
 */
#ifndef AX_SENSOR_H
#define AX_SENSOR_H

typedef unsigned int ax_u32;
typedef unsigned char ax_u8;

/* SPR access via the compiler's __sfr qualifier. Note that GCC emits a *dynamic*
 * l.mtspr/l.mfspr (address materialised into a register, rA != r0) for any
 * read-modify-write pattern, or whenever the register address is not a literal.
 * That is functionally correct on hardware, but it cannot be checked statically
 * by the bench ELF policy validator, so probes that need that check must both
 * use literal register addresses and be built with AX_SENSOR_SPR_LITERAL, which
 * switches to an r0-based inline-asm form with a constant immediate.
 * Override both macros to host the driver against a model. */
#ifndef AX_SENSOR_READ
#ifdef AX_SENSOR_SPR_LITERAL
#define AX_SENSOR_READ(n) __extension__({ \
    ax_u32 __ax_spr_v; \
    __asm__ volatile ("l.mfspr %0,r0,%1" : "=r"(__ax_spr_v) : "i"(n)); \
    __ax_spr_v; })
#else
#define AX_SENSOR_READ(n) (*(volatile __sfr ax_u32 *)(n))
#endif
#endif
#ifndef AX_SENSOR_WRITE
#ifdef AX_SENSOR_SPR_LITERAL
#define AX_SENSOR_WRITE(n, v) \
    __asm__ volatile ("l.mtspr r0,%0,%1" :: "r"((ax_u32)(v)), "i"(n) : "memory")
#else
#define AX_SENSOR_WRITE(n, v) (*(volatile __sfr ax_u32 *)(n) = (ax_u32)(v))
#endif
#endif

/* IIC blocks sit in SPRGROUP_SFR2; each bus has the same six registers. */
#define AX_IIC0_BASE    0x9090u
#define AX_IIC1_BASE    0x90ccu
#define AX_IIC_OFF_BAUD  0x00u   /* baud divisor, APB_CLK/(5*baud) - 1 */
#define AX_IIC_OFF_CON   0x04u   /* bit 7 = controller enable */
#define AX_IIC_OFF_IDATA 0x08u   /* received byte (rx) */
#define AX_IIC_OFF_ODATA 0x0cu   /* byte to transmit (tx) */
#define AX_IIC_OFF_CMD   0x10u   /* write-only; see bits below */
#define AX_IIC_OFF_STA   0x14u   /* bit 0 = done, bit 7 = NACK/busy */

/* Which bus the convenience wrappers use. 0 = IIC0, 1 = IIC1. */
#ifndef AX_IIC_BUS
#define AX_IIC_BUS 0
#endif
#define AX_IIC_BASE (AX_IIC_BUS ? AX_IIC1_BASE : AX_IIC0_BASE)

#define AX_IIC_START   0x00000080u  /* (1<<7) generate start */
#define AX_IIC_STOP    0x00000040u  /* (1<<6) generate stop */
#define AX_IIC_READ    0x00000028u  /* (1<<5)|(1<<3) receive one byte */
#define AX_IIC_WRITE   0x00000010u  /* (1<<4) transmit ODATA */
#define AX_IIC_CLEAR   0x00000001u  /* bit 0: clear the done flag */
#define AX_IIC_DONE    0x00000001u  /* STA bit 0: transfer finished */
#define AX_IIC_NACK    0x00000080u  /* STA bit 7: set means NACK/busy */

/* How long to wait for the done flag before giving up. A stuck bus must return
 * an error rather than spin forever inside a bounded bench payload. */
#ifndef AX_IIC_TIMEOUT
#define AX_IIC_TIMEOUT 200000u
#endif

/* GPIO / pin-mux registers (SPRGROUP_SFR0, base 0x8000). Port index 3 is PE. */
#define AX_GPIO_DATA_PE 0x800cu
#define AX_GPIO_DIR_PE  0x804cu
#define AX_GPIO_PLU_PE  0x808cu
#define AX_GPIO_MAP_PE  0x810cu
#define AX_GPIO_DGL_PE  0x838cu
#define AX_PMAPCON2     0x8348u
#define AX_PE0          0x00000001u
#define AX_PE1          0x00000002u
#define AX_PE10         0x00000400u
#define AX_PE13         0x00002000u
#define AX_PE15         0x00008000u

/* Baud divisor is APB_CLK/(5*baudrate) - 1 per ax32xx_iic0Init. APB_CLK is
 * COMBO_CLOCK, which the SDK sets to 144 MHz on real silicon (72 MHz only on
 * the FPGA platform). Override AX_SENSOR_APB_CLK if the board differs. */
#ifndef AX_SENSOR_APB_CLK
#define AX_SENSOR_APB_CLK 144000000u
#endif
#define AX_IIC_BAUD_DIVISOR(hz) (AX_SENSOR_APB_CLK / (5u * (hz)) - 1u)

/* Selected pin group per bus, from the AX3292 platform configuration:
 *   IIC0 uses IIC0_POS2 -> PMAPCON2 bits 2:0 = 3, SCL PE13 / SDA PE15
 *   IIC1 uses IIC1_POS6 -> PMAPCON2 bits 5:3 = 7, SCL PE0  / SDA PE1
 * Enabling the controller without this leaves the bus silent on the pins. */
static inline void ax_iic_pins_init(ax_u32 bus)
{
    ax_u32 scl, sda;
    if (bus) {
        scl = AX_PE0; sda = AX_PE1;
        AX_SENSOR_WRITE(AX_PMAPCON2, (AX_SENSOR_READ(AX_PMAPCON2) & ~(0x7u << 3)) | (7u << 3));
    } else {
        scl = AX_PE13; sda = AX_PE15;
        AX_SENSOR_WRITE(AX_PMAPCON2, (AX_SENSOR_READ(AX_PMAPCON2) & ~0x7u) | 3u);
    }
    const ax_u32 both = scl | sda;
    AX_SENSOR_WRITE(AX_GPIO_DGL_PE, AX_SENSOR_READ(AX_GPIO_DGL_PE) | both);
    AX_SENSOR_WRITE(AX_GPIO_MAP_PE, AX_SENSOR_READ(AX_GPIO_MAP_PE) | both);
    AX_SENSOR_WRITE(AX_GPIO_DATA_PE, AX_SENSOR_READ(AX_GPIO_DATA_PE) | both);
    AX_SENSOR_WRITE(AX_GPIO_DIR_PE, (AX_SENSOR_READ(AX_GPIO_DIR_PE) & ~scl) | sda);
    AX_SENSOR_WRITE(AX_GPIO_PLU_PE, AX_SENSOR_READ(AX_GPIO_PLU_PE) | both);
}

/* ---- bus primitives, on an explicit base ---------------------------------- */

static inline int ax_iic_wait_done_at(ax_u32 base)
{
    ax_u32 n = AX_IIC_TIMEOUT;
    while (n--) {
        if (AX_SENSOR_READ(base + AX_IIC_OFF_STA) & AX_IIC_DONE) { return 1; }
    }
    return 0;
}

static inline void ax_iic_start_at(ax_u32 base)
{
    AX_SENSOR_WRITE(base + AX_IIC_OFF_CMD, AX_IIC_START);
}

static inline void ax_iic_stop_at(ax_u32 base)
{
    AX_SENSOR_WRITE(base + AX_IIC_OFF_CMD, AX_IIC_STOP);
    (void)ax_iic_wait_done_at(base);
    AX_SENSOR_WRITE(base + AX_IIC_OFF_CMD,
                    AX_SENSOR_READ(base + AX_IIC_OFF_CMD) | AX_IIC_CLEAR);
}

static inline int ax_iic_send_at(ax_u32 base, ax_u8 byte)
{
    AX_SENSOR_WRITE(base + AX_IIC_OFF_ODATA, byte);
    AX_SENSOR_WRITE(base + AX_IIC_OFF_CMD,
                    AX_SENSOR_READ(base + AX_IIC_OFF_CMD) | AX_IIC_WRITE);
    if (!ax_iic_wait_done_at(base)) { return 0; }
    AX_SENSOR_WRITE(base + AX_IIC_OFF_CMD,
                    AX_SENSOR_READ(base + AX_IIC_OFF_CMD) | AX_IIC_CLEAR);
    return 1;
}

/* 1 when the addressed device acknowledged. */
static inline int ax_iic_ack_at(ax_u32 base)
{
    return (AX_SENSOR_READ(base + AX_IIC_OFF_STA) & AX_IIC_NACK) == 0;
}

static inline int ax_iic_recv_at(ax_u32 base, ax_u8 *out)
{
    AX_SENSOR_WRITE(base + AX_IIC_OFF_CMD,
                    AX_SENSOR_READ(base + AX_IIC_OFF_CMD) | AX_IIC_READ);
    if (!ax_iic_wait_done_at(base)) { return 0; }
    AX_SENSOR_WRITE(base + AX_IIC_OFF_CMD,
                    AX_SENSOR_READ(base + AX_IIC_OFF_CMD) | AX_IIC_CLEAR);
    *out = (ax_u8)(AX_SENSOR_READ(base + AX_IIC_OFF_IDATA) & 0xffu);
    return 1;
}

/* Mux the pins, set the baud rate and enable the controller, matching the SDK's
 * ax32xx_iic0Init/ax32xx_iic1Init. Enabling without the pin mux leaves it silent. */
static inline void ax_iic_enable_bus(ax_u32 bus, ax_u32 baud_divisor)
{
    ax_u32 base = bus ? AX_IIC1_BASE : AX_IIC0_BASE;
    ax_iic_pins_init(bus);
    AX_SENSOR_WRITE(base + AX_IIC_OFF_BAUD, baud_divisor);
    AX_SENSOR_WRITE(base + AX_IIC_OFF_CON,
                    AX_SENSOR_READ(base + AX_IIC_OFF_CON) | (1u << 7));
}

/* Read one 8-bit-addressed register. Returns 0 on timeout or NACK, so an
 * unpowered sensor yields a definite failure instead of a bogus 0xff value. */
static inline int ax_sensor_read_reg_at(ax_u32 base, ax_u8 slave, ax_u8 reg, ax_u8 *out)
{
    ax_iic_start_at(base);
    if (!ax_iic_send_at(base, slave) || !ax_iic_ack_at(base)) { ax_iic_stop_at(base); return 0; }
    if (!ax_iic_send_at(base, reg)   || !ax_iic_ack_at(base)) { ax_iic_stop_at(base); return 0; }
    ax_iic_stop_at(base);

    ax_iic_start_at(base);
    if (!ax_iic_send_at(base, (ax_u8)(slave | 1u)) || !ax_iic_ack_at(base)) {
        ax_iic_stop_at(base); return 0;
    }
    if (!ax_iic_recv_at(base, out)) { ax_iic_stop_at(base); return 0; }
    ax_iic_stop_at(base);
    return 1;
}

static inline int ax_sensor_write_reg_at(ax_u32 base, ax_u8 slave, ax_u8 reg, ax_u8 value)
{
    ax_iic_start_at(base);
    if (!ax_iic_send_at(base, slave) || !ax_iic_ack_at(base)) { ax_iic_stop_at(base); return 0; }
    if (!ax_iic_send_at(base, reg)   || !ax_iic_ack_at(base)) { ax_iic_stop_at(base); return 0; }
    if (!ax_iic_send_at(base, value) || !ax_iic_ack_at(base)) { ax_iic_stop_at(base); return 0; }
    ax_iic_stop_at(base);
    return 1;
}

/* ---- convenience wrappers on the configured bus ---------------------------- */

static inline void ax_iic_enable(ax_u32 baud_divisor)
{
    ax_iic_enable_bus(AX_IIC_BUS, baud_divisor);
}

static inline int ax_sensor_read_reg(ax_u8 slave, ax_u8 reg, ax_u8 *out)
{
    return ax_sensor_read_reg_at(AX_IIC_BASE, slave, reg, out);
}

static inline int ax_sensor_write_reg(ax_u8 slave, ax_u8 reg, ax_u8 value)
{
    return ax_sensor_write_reg_at(AX_IIC_BASE, slave, reg, value);
}

/* Original KC02 imager. The stock resource table identifies H63P by a single
 * byte: register 0x0b == 0x48. 7-bit address 0x20, so 0x40/0x41 on the wire. */
/* 8-bit write address 0x80, i.e. 7-bit 0x40. Measured on hardware: a software-IIC
 * read of register 0x0b returns 0x48 at 0x80 and never acknowledges at 0x40.
 * The earlier 0x40 assumption came from the emulator's model, not from the
 * device, and it is why both our hardware-IIC attempts and stock's own
 * hal_iic08bitAddrReadData(0x40, 0x0b) read 0xff. */
#define AX_SENSOR_SLAVE_ADDR 0x80u
#define AX_SENSOR_ID_REG     0x0bu
#define AX_SENSOR_H63P_ID    0x48u

/* Returns 1 and stores the ID when the H63P answers as expected. */
static inline int ax_sensor_detect_h63p_at(ax_u32 base, ax_u8 *id_out)
{
    ax_u8 id = 0;
    if (!ax_sensor_read_reg_at(base, AX_SENSOR_SLAVE_ADDR, AX_SENSOR_ID_REG, &id)) {
        return 0;
    }
    if (id_out) { *id_out = id; }
    return id == AX_SENSOR_H63P_ID;
}

static inline int ax_sensor_detect_h63p(ax_u8 *id_out)
{
    return ax_sensor_detect_h63p_at(AX_IIC_BASE, id_out);
}

#endif /* AX_SENSOR_H */
