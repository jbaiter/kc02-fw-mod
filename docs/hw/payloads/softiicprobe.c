/* Sensor ID read over the SOFTWARE (bit-banged) IIC bus.
 *
 * This is the test that settles the sensor question. Every previous attempt drove
 * the hardware IIC0/IIC1 controllers, which sit on the same pins but with the
 * hardware peripheral function selected. Decompiling the stock sensor path showed
 * it is bit-banged GPIO: software_iic_gpioInit maps PE13/PE15 with MAP = 0, i.e.
 * plain GPIO, and software_iic_start/stop/wait_ack drive the protocol by hand.
 * That is why both hardware buses failed for us and for stock's own helper.
 *
 * Expected: register 0x0b reads 0x48, the H63P identity the stock resource table
 * checks for.
 *
 * OUTPUT: revision, mode, ack/id results, then GPIO state after the transfer.
 */
#include "common.h"
#include "ax_iic_soft.h"

/* Two candidate 8-bit write addresses. The project's own annotation of
 * sensor_init says "I2C 7-bit 0x40 (write 0x80 / read 0x81)", while the
 * emulator's H63P model matches 0x40 (7-bit 0x20). Try both rather than pick. */
#define AX_IIC_ADDR_A     0x40u
#define AX_IIC_ADDR_B     0x80u
#define AX_IIC_H63P_IDREG 0x0bu
#define AX_IIC_H63P_ID    0x48u

__attribute__((section(".entry")))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    u32 i;
    for (i = 0; i < 26; ++i) out[i] = 0;

    out[0] = 7;                       /* probe revision */
    out[1] = in[0];
    out[2] = AX_SOFT_READ(AX_GPIO3_MAP);
    out[3] = AX_SOFT_READ(AX_GPIO3_DIR);
    out[4] = AX_SOFT_READ(AX_GPIO3_DATA);

    ax_soft_init();
    out[5] = AX_SOFT_READ(AX_GPIO3_MAP);   /* MAP must read back with bits cleared */
    out[6] = AX_SOFT_READ(AX_GPIO3_DIR);

    ax_u8 id = 0, other = 0;

    /* Address A (0x40, 7-bit 0x20): raw ack, then the identity register. */
    ax_soft_start();
    ax_soft_send_byte(AX_IIC_ADDR_A);
    out[7] = (u32)ax_soft_wait_ack();
    ax_soft_stop();
    out[8] = (u32)ax_soft_read_reg(AX_IIC_ADDR_A, AX_IIC_H63P_IDREG, &id);
    out[9] = id;

    /* Address B (0x80, 7-bit 0x40): the address the sensor_init annotation names. */
    ax_soft_start();
    ax_soft_send_byte(AX_IIC_ADDR_B);
    out[14] = (u32)ax_soft_wait_ack();
    ax_soft_stop();
    id = 0;
    out[15] = (u32)ax_soft_read_reg(AX_IIC_ADDR_B, AX_IIC_H63P_IDREG, &id);
    out[16] = id;

    /* A second register on B, so an answering bus gives two differing bytes. */
    out[17] = (u32)ax_soft_read_reg(AX_IIC_ADDR_B, 0x00u, &other);
    out[18] = other;

    out[10] = (u32)ax_soft_read_reg(AX_IIC_ADDR_A, 0x00u, &other);
    out[11] = other;

    out[12] = ((out[8] == 1 && out[9] == AX_IIC_H63P_ID) ||
               (out[15] == 1 && out[16] == AX_IIC_H63P_ID)) ? 1u : 0u;
    out[13] = AX_SOFT_READ(AX_GPIO3_DATA);

    finish(buffer, length, parameter, 14);
}
