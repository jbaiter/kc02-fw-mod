/* Stock KC02 firmware reference: the ADC used for the button ladder.
 *
 * Decompiled from firmware/original.bin. The SDK has the ADC driver
 * (ax32xx/src/ax32xx_adc.c); what matters here is the CALL SEQUENCE, because
 * omitting one step makes the controller look alive and never complete a
 * conversion -- which is how the buttons came to look dead while the UI was
 * demonstrably rendering.
 */
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define REG(n) (*(volatile u32 *)(n))

#define ADCCON 0x9170u
#define ADCBAUD 0x9174u
#define ADCDAT 0x9178u

#define ADC_EN   0x10u   /* enable, ORed with the channel number in bits 3:0 */
#define ADC_EOC  0x40u   /* busy: the SDK SPINS WHILE THIS IS SET */
#define ADC_GO   0x80u   /* start a conversion */

#define SYS_CLK_ADC 29u  /* clock index; gate lives in 0x8840 bit 29 */

/* 0x0202f7a0  ax32xx_adcEnable(ch, en)
 *
 * TWO effects, and the second is easy to miss: it selects the channel AND
 * enables the ADC module clock. ax32xx_sysClkSet(type, 1) CLEARS the gate bit, so
 * a cleared bit means enabled -- see power_clock_gpio.c. A port that only wrote
 * ADCCON left the block unclocked, the busy bit never cleared, every read timed
 * out, and every button press reported "no key".                    VERIFIED
 */
void ax32xx_adcEnable(u8 ch, u8 en)
{
    if (ch >= 0xc) { return; }
    if ((en & 1) == 0) {
        REG(ADCCON) &= ~ADC_EN;
    } else {
        REG(ADCCON) = ch | ADC_EN;
        ax32xx_sysClkSet(SYS_CLK_ADC, 1);        /* clears the 0x8840 gate bit */
    }
}

/* 0x0202f808  ax32xx_adcSetBaudrate(baud)
 * Divider is 144 MHz / (baud * 2) - 1, masked to 7 bits. The stock keypad path
 * uses 1 MHz, giving 71.                                                  VERIFIED
 */
void ax32xx_adcSetBaudrate(u32 baud)
{
    REG(ADCBAUD) = (144000000u / (baud * 2u) - 1u) & 0x7fu;
}

/* 0x0202f838  ax32xx_adcConverStart(): set GO. */
void ax32xx_adcConverStart(void) { REG(ADCCON) |= ADC_GO; }

/* 0x0202f85c  ax32xx_adcRead()
 * Bounded spin while the EOC/busy bit is set, then read the 10-bit result.
 * Bound is 0x8ffff iterations.                                           VERIFIED
 */
u16 ax32xx_adcRead(void)
{
    u32 timeout = 0x8ffffu;
    while ((REG(ADCCON) & ADC_EOC) != 0) {
        if (--timeout == 0) { break; }
    }
    return (u16)(REG(ADCDAT) & 0x3ffu);
}

/* ---------------------------------------------------------------------------
 * 0x0203814c  hal_adcGetChannel(ch) -- the whole sequence, in order.
 *
 * Getting this order wrong is what breaks keypad reads, so it is worth stating
 * plainly: enable (which also ungates the clock), set the baud rate, start, read.
 * ------------------------------------------------------------------------ */
int hal_adcGetChannel(u8 ch)
{
    ax32xx_adcEnable(ch, 1);
    ax32xx_adcSetBaudrate(1000000);
    ax32xx_adcConverStart();
    return ax32xx_adcRead();
}

/* ---------------------------------------------------------------------------
 * MEASURED behaviour on the original KC02:
 *
 *   stock channel-3 conversion returns 1023 with no button pressed
 *   ADCCON reads back 0x13 before and after; GO and BUSY both self-clear
 *   idle ladder 1021..1023, and OK held reads 388..392 against a centre of 386
 *                                                                     VERIFIED
 *
 * Ladder windows measured from the fixture (see
 * tests/fixtures/hardware/original-2026-10-15.json):
 *
 *   ok     386      right  776      left   252
 *   up     657      down   510      escape  12        half-width 35
 *
 * The escape window is 0..47 because centre 12 minus 35 clamps at zero.
 * ------------------------------------------------------------------------ */
