/* Stock KC02 firmware reference: power rails, module clock gates, pin muxing.
 *
 * Decompiled from firmware/original.bin. These three are the source of two of the
 * hardest bugs in this project, because a wrong argument is a silent no-op or a
 * silently gated peripheral rather than an error.
 */
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define REG(n) (*(volatile u32 *)(n))

/* ---------------------------------------------------------------------------
 * 0x0202b284  ax32xx_sysLDOSet(ldo, sel, en)
 *
 * The switch only implements indices 0..5. ANY other value falls to the default
 * branch and returns immediately -- it does not clamp, warn or fail. A probe in
 * this project called sysLDOSet(6, 1, 0) and concluded the sensor rails were on,
 * when in fact nothing at all happened and the imager was never powered.
 *
 * Note also the inverted enable: in every branch, en == 0 CLEARS the rail bit.
 * ------------------------------------------------------------------------ */
enum {
    SYS_LDO_HSEN = 0,   /* sensor AVDD; selector lands in bits 4:0, enable bit 5   */
    SYS_LDO_LSEN = 1,   /* sensor DVDD; selector lands in bits 11:7, enable bit 12 */
    SYS_LDO_BGOE = 2,
    SYS_LDO_V1_2 = 3,
    SYS_LDO_SDC  = 4,
    SYS_LDO_DDR  = 5,
};

void ax32xx_sysLDOSet(u8 ldo, u8 sel, u8 en)
{
    u32 v = REG(0x8868);
    u32 s = sel & 0x1f;
    switch (ldo) {
    case SYS_LDO_HSEN:
        if (en) {
            /* The stock code only takes the HV path when sel > 22. The H63P's
             * AVDD selector is 16, so it does not. */
            if (s > 0x16) { ax32xx_rtcSenHVEnable(1); REG(0x8868) = REG(0x8868) | 0x40u; }
            REG(0x8868) = (v & 0xffffffe0u) | 0x20u | s;
        } else {
            REG(0x8868) = (v & 0xffffffe0u);
        }
        break;
    case SYS_LDO_LSEN:
        if (en) { REG(0x8868) = (v & 0xfffff07fu) | 0x1000u | (s << 7); }
        else    { REG(0x8868) = v & ~(0x1000u | (0x1fu << 7)); }
        break;
    /* cases 2..5 omitted: same shape, different bit positions */
    default:
        return;                        /* <-- silent no-op for any other index */
    }
}

/* ---------------------------------------------------------------------------
 * 0x0202b468  ax32xx_sysClkSet(clk_type, en)
 *
 * Module clock gates live in 0x8840 (indices 0..31) and 0x8844 (32..63), one bit
 * per block. THE POLARITY IS INVERTED: en == 0 SETS the bit, en != 0 CLEARS it.
 * A cleared bit is therefore the enabled state, which reads backwards from the
 * call site at every use.
 *
 * Two bugs in this project came from missing gates entirely: the ADC (index 29)
 * and the CSI/DVP block (index 27). Both made the peripheral half-work rather
 * than fail, because everything except the clock was configured correctly.
 * ------------------------------------------------------------------------ */
void ax32xx_sysClkSet(u8 clk_type, u8 en)
{
    u32 *gate = (clk_type < 0x20) ? (u32 *)0x8840 : (u32 *)0x8844;
    u32 bit = (clk_type < 0x20) ? clk_type : (u32)(clk_type - 0x20) & 0xff;
    u32 v = *gate;
    *gate = (en == 0) ? (v | (1u << bit)) : (v & ~(1u << bit));
}

/* Clock indices seen in this project. */
enum {
    SYS_CLK_TIMER0  = 10,
    SYS_CLK_IIC0    = 14,
    SYS_CLK_IIC1    = 23,
    SYS_CLK_ADC     = 29,
    SYS_CLK_DVP_CSI = 27,
};

/* ---------------------------------------------------------------------------
 * 0x0203444c  ax32xx_csiMclkSet(freq)
 *
 * Divider is 288000000/freq - 1 in bits 12:8 of 0x884c, plus the CSI pin group
 * in 0x8348 bits 11:10 and the PE10 output mux. Verified values: 24 MHz -> 11,
 * 16 MHz -> 17. The field is 5 bits, so the mclk must stay above about 9 MHz.
 *                                                                     VERIFIED
 * ------------------------------------------------------------------------ */
void ax32xx_csiMclkSet(u32 freq)
{
    REG(0x8850) &= ~0x04000000u;
    REG(0x8850) |=  0x04000000u;
    ax32xx_gpioSFRSet(0xf, 2);                 /* CSI pin group */
    ax32xx_gpioMapSet(3, 0x400, 1);            /* PE10 to the CSI function */
    ax32xx_gpioDirSet(3, 0x400, 0);            /* PE10 output */
    REG(0x8848) &= ~0x20u;
    REG(0x884c) = (REG(0x884c) & ~0x1f00u) | (((288000000u / freq) - 1u) << 8);
}

/* ---------------------------------------------------------------------------
 * GPIO helpers. Port index 3 is PE; GPIO_xxx(a) = SPRGROUP_SFR0 + ((a + base) << 2)
 * with SPRGROUP_SFR0 = 0x8000.
 *
 * NOTE the two-level mux: PMAPCON2 (0x8348) selects which PERIPHERAL is on a pin
 * group, while GPIO_MAP(ch) selects GPIO versus SFR FUNCTION per pin. The
 * software I2C needs MAP = 0 but does not touch PMAPCON2, which is why the two
 * must not be confused.                                              VERIFIED
 * ------------------------------------------------------------------------ */
#define GPIO_DATA(a) (0x8000u + (((a) + 0x00u) << 2))
#define GPIO_DIR(a)  (0x8000u + (((a) + 0x10u) << 2))
#define GPIO_PLU(a)  (0x8000u + (((a) + 0x20u) << 2))
#define GPIO_PLD(a)  (0x8000u + (((a) + 0x30u) << 2))
#define GPIO_MAP(a)  (0x8000u + (((a) + 0x40u) << 2))
#define GPIO_DRV(a)  (0x8000u + (((a) + 0x50u) << 2))
#define GPIO_DGL(a)  (0x8000u + (((a) + 0xe0u) << 2))

void ax32xx_gpioMapSet(u8 ch, u32 pin, u8 map)
{
    if (map) { REG(GPIO_MAP(ch)) |=  pin; }
    else     { REG(GPIO_MAP(ch)) &= ~pin; }
}

void ax32xx_gpioDirSet(u8 ch, u32 pin, u8 dir)
{
    if (dir == 0) { REG(GPIO_DIR(ch)) &= ~pin; }   /* 0 = output */
    else          { REG(GPIO_DIR(ch)) |=  pin; }
}

void ax32xx_gpioDataSet(u8 ch, u32 pin, u8 data)
{
    if (data) { REG(GPIO_DATA(ch)) |=  pin; }
    else      { REG(GPIO_DATA(ch)) &= ~pin; }
}

u8 ax32xx_gpioDataGet(u8 ch, u32 pin) { return (REG(GPIO_DATA(ch)) & pin) ? 1 : 0; }

void ax32xx_gpioDigitalSet(u8 ch, u32 pin, u8 digital)
{
    if (digital) { REG(GPIO_DGL(ch)) |=  pin; }
    else         { REG(GPIO_DGL(ch)) &= ~pin; }
}

void ax32xx_gpioPullSet(u8 ch, u32 pin, u8 pull)
{
    if (pull & 1) { REG(GPIO_PLU(ch)) |=  pin; } else { REG(GPIO_PLU(ch)) &= ~pin; }
    if (pull & 2) { REG(GPIO_PLD(ch)) |=  pin; } else { REG(GPIO_PLD(ch)) &= ~pin; }
}

/* 0x0202ea40 area -- PE pin numbers used by the sensor bus. */
#define PE10 0x0400u   /* CSI MCLK output */
#define PE11 0x0800u   /* camera power select (complementary pairs) */
#define PE13 0x2000u   /* software-I2C SCL */
#define PE15 0x8000u   /* software-I2C SDA */

/* ---------------------------------------------------------------------------
 * 0x0202ec40 is the IIC0 init front end: hal_iic0Init (0x02038730) calls it with
 * a 200 kHz rate and then sensor_iic_stop(), i.e. it ends with a SOFTWARE I2C
 * stop. That is the fingerprint of USE_SOFTWARE_IIC0 in this build.
 * ------------------------------------------------------------------------ */
