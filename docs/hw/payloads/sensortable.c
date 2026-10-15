/* Extract the stock H63P init table by calling the stock autodetect.
 *
 * The table is data, not code, so it cannot be decompiled. It does not need to
 * be: sensorAutoCheck(initbuf) copies the selected sensor's table into a
 * caller-supplied buffer. Borrow the table rather than guess its encoding.
 *
 * The critical detail found by decompiling the stock path: the sensor is NOT on
 * a hardware IIC controller. sensor_iic_start/stop/check_ack are thin wrappers
 * over software_iic_start/stop/wait_ack, i.e. GPIO bit-banging, and this build
 * takes the SDK's USE_SOFTWARE_IIC0 path. That is why the hardware IIC0 and IIC1
 * controllers both sat on pins nothing was listening to, and why stock's own
 * hardware-IIC helper read 0xff. So the bit-banged bus must be initialised with
 * software_iic_init() before autodetect can read anything.
 *
 * The buffer goes in the measured free span 0x020db200-0x020e30ff, which the RAM
 * map shows is all zero, referenced by no static global, and unchanged across a
 * full test batch. The host reads it back afterwards.
 *
 * OUTPUT: ops pointer, buffer address, buffer length, then the first 44 bytes.
 */
#include "common.h"

typedef u32 (*fn1)(u32);
typedef void (*fn0)(void);
typedef u32 (*autocheck_t)(unsigned char *buffer);

#define MCLK_FN       0x020381A8u   /* csi_set_mclk */
#define SW_IIC_INIT   0x0202EA68u   /* software_iic_init: GPIO setup for bit-bang */
#define SW_IIC_GPIO   0x0202E884u   /* software_iic_gpioInit (board pin config) */
#define AUTOCHK       0x02058170u   /* sensorAutoCheck */
#define SCRATCH       0x020DC000u   /* measured-free span, clear of the packet */
#define SCRATCH_LEN   0x1000u

__attribute__((section(".entry")))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *out = OUTPUT(buffer);
    u32 i;
    for (i = 0; i < 26; ++i) out[i] = 0;

    /* Clear the scratch first, so what autodetect leaves behind is unambiguous. */
    for (i = 0; i < SCRATCH_LEN; i += 4u) {
        *(volatile u32 *)(SCRATCH + i) = 0xA5A5A5A5u;
    }

    ((fn1)MCLK_FN)(16000000u);      /* 16 MHz for the id check, as sensorInit does */
    ((fn0)SW_IIC_GPIO)();           /* board pin configuration for the bit-bang */
    ((fn0)SW_IIC_INIT)();           /* software IIC init */

    u32 ops = ((autocheck_t)AUTOCHK)((unsigned char *)SCRATCH);
    out[0] = ops;
    out[1] = SCRATCH;
    out[2] = SCRATCH_LEN;
    for (i = 0; i < 11; ++i) {
        out[3 + i] = *(volatile u32 *)(SCRATCH + i * 4u);
    }
    finish(buffer, length, parameter, 14);
}
