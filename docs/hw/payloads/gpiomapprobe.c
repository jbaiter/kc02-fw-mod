/* Is the per-pin GPIO function select writable?
 *
 * The software-IIC path depends on clearing the SFR-function bit for PE13/PE15
 * (software_iic_gpioInit calls gpioMapSet(3, pins, 0)). An earlier probe read
 * GPIO_MAP back unchanged after clearing it, which would mean the pins stay on
 * the hardware IIC0 function and a bit-bang can never reach them.
 *
 * This writes the register and reads it straight back, then restores it, so the
 * question is settled rather than inferred.
 *
 * OUTPUT: MAP original, then after writing 0x0000, 0xffff, and the original;
 *         plus PMAPCON2 and the direction register for context.
 */
#include "common.h"

#define REG(n) (*(volatile __sfr u32 *)(n))
#define GPIO3_DATA 0x800cu
#define GPIO3_DIR  0x804cu
#define GPIO3_PLU  0x808cu
#define GPIO3_MAP  0x810cu
#define GPIO3_DRV  0x814cu
#define GPIO3_DGL  0x838cu
#define PMAPCON2   0x8348u
#define SOFT_SCL   0x2000u
#define SOFT_SDA   0x8000u

__attribute__((section(".entry")))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *out = OUTPUT(buffer);
    u32 i;
    for (i = 0; i < 26; ++i) out[i] = 0;

    u32 o_map = REG(GPIO3_MAP);
    out[0] = o_map;
    out[1] = REG(PMAPCON2);
    out[2] = REG(GPIO3_DIR);
    out[3] = REG(GPIO3_DGL);
    out[4] = REG(GPIO3_DRV);
    out[5] = REG(GPIO3_PLU);

    /* Is the register writable at all? */
    REG(GPIO3_MAP) = 0x00000000u;
    out[6] = REG(GPIO3_MAP);
    REG(GPIO3_MAP) = 0xffffffffu;
    out[7] = REG(GPIO3_MAP);

    /* The operation the bit-bang actually needs: clear just the two pins. */
    REG(GPIO3_MAP) = o_map & ~(SOFT_SCL | SOFT_SDA);
    out[8] = REG(GPIO3_MAP);
    out[9] = o_map & ~(SOFT_SCL | SOFT_SDA);   /* what we asked for */

    /* Put it back. */
    REG(GPIO3_MAP) = o_map;
    out[10] = REG(GPIO3_MAP);

    finish(buffer, length, parameter, 11);
}
