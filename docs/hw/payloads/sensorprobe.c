/* Sensor identity probe, using the firmware's own bring-up pieces:
     LDO power (0x0202B284(6,1,0)) -> CSI MCLK 16 MHz (0x020381A8) -> IIC enable (0x02038730)
     -> sensorAutoCheck (0x02058170), which reads the sensor ID register and compares it
        against the flash resource's expected value (H63P: reg 0x0B == 0x48).
   The CSI-task registration that sensorInit does afterwards is deliberately NOT called, so no
   capture pipeline starts. The sensor LDO is switched off again before returning.
   Also performs an independent IIC read of reg 0x0B via hal_iic08bitAddrReadData. */
#include "common.h"
typedef u32 (*fn3)(u32, u32, u32);
typedef u32 (*fn1)(u32);
typedef u32 (*fn0)(void);
typedef void (*rd8)(u32 slave, u32 addr, u32 *data);
#define LDO_FN    0x0202B284u
#define MCLK_FN   0x020381A8u
#define IIC_EN    0x02038730u
#define AUTOCHK   0x02058170u
#define IIC_RD    0x02038818u
#define ITEM_RAM  0x020E4BA0u
#define OPS_RAM   0x020E32A0u
#define TAB_RAM   0x020D1EA0u
#define HOLDER    0x020D2EA0u
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    fn3 ldo = (fn3)LDO_FN; fn1 mclk = (fn1)MCLK_FN; fn0 iicEn = (fn0)IIC_EN;
    fn0 autochk = (fn0)AUTOCHK; rd8 rd = (rd8)IIC_RD;
    u32 ops = 0; u32 idbuf = 0xFFFFFFFFu;
    for (u32 k = 0; k < 26; ++k) out[k] = 0xEEEEEEEEu;
    if (in[0] == 1u) {
        ldo(6, 1, 0);                 /* sensor LDO on */
        mclk(16000000u);              /* CSI MCLK 16 MHz for ID check */
        iicEn();                      /* IIC bus enable */
        ops = autochk();              /* flash resource + IIC probe of reg 0x0B */
        rd(0x40u, 0x0Bu, &idbuf);     /* independent read of the H63P id register */
        out[0] = ops;                 /* 0x020E32A0 = real sensor, 0x02083CB0 = test-pattern fallback */
        out[1] = idbuf & 0xFFu;       /* live IIC byte: expect 0x48 */
        out[2] = *(volatile u32 *)HOLDER;
        for (u32 k = 0; k < 20; ++k) out[3 + k] = ((volatile u32 *)ITEM_RAM)[k];
        out[23] = ((volatile u32 *)ITEM_RAM)[0x34 / 4];   /* inline name, expect "H63P" */
        out[24] = ((volatile u32 *)ITEM_RAM)[0x48 / 4];   /* id field, expect 0x48 */
        out[25] = ((volatile u32 *)TAB_RAM)[0];           /* init-table copy, first pair */
        mclk(0);
        ldo(6, 0, 0);                 /* sensor LDO off again */
    } else {
        out[0] = *(volatile u32 *)OPS_RAM;
        out[1] = *(volatile u32 *)ITEM_RAM;
    }
    finish(buffer, length, parameter, 1);
}
