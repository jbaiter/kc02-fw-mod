/* AX3292 camera power, MCLK and pin-mux bring-up.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Transcribed from the stock firmware's own routines, so Luma drives the same
 * registers with the same values:
 *   ax32xx_sysLDOSet   0x0202b284   -> SPR 0x8868
 *   ax32xx_csiMclkSet  0x0203444c   -> SPR 0x884c/0x8850/0x8848 + PE10 mux
 *   hal_iic0Init       0x02038730   -> ax32xx_iic0Init(200000) + stop
 *
 * This exists because the bench probe that tried to power the sensor called
 * ax32xx_sysLDOSet with index 6 and en=0. The stock LDO indices only reach 5,
 * so that call was a no-op: the imager was never powered, which is why the ID
 * read returned 0xff. The sensor's rails are SYS_LDO_LSEN(1)/SYS_LDO_HSEN(0)
 * with the per-sensor DVDD/AVDD selectors.
 *
 * Original KC02 H63P values, read from its _Sensor_Adpt_ record in RAM:
 *   mclk 24 MHz, DVDD 6, AVDD 16, VDDIO 16, 1280x720, YUV.
 */
#ifndef AX_CAM_POWER_H
#define AX_CAM_POWER_H

#include "ax_sensor.h"

#ifndef AX_CAM_READ
#define AX_CAM_READ(n) AX_SENSOR_READ(n)
#endif
#ifndef AX_CAM_WRITE
#define AX_CAM_WRITE(n, v) AX_SENSOR_WRITE(n, v)
#endif

#define AX_CAM_SYS_LDO      0x8868u   /* system LDO select/enable */
#define AX_CAM_MCLK_DIV     0x884cu   /* bits 12:8 = 288MHz/freq - 1 */
#define AX_CAM_MCLK_CTL     0x8850u
#define AX_CAM_MCLK_EN      0x8848u
/* Pin-mux registers are shared with the IIC0 bring-up; see ax_sensor.h. */
#define AX_CAM_PMAPCON2     AX_PMAPCON2
#define AX_CAM_GPIO_MAP_PE  AX_GPIO_MAP_PE
#define AX_CAM_GPIO_DIR_PE  AX_GPIO_DIR_PE
#define AX_CAM_PE10         AX_PE10

/* LDO indices, from SYS_LDO_E. There is no index above 5. */
#define AX_LDO_HSEN 0u   /* sensor AVDD, selector in bits 4:0 */
#define AX_LDO_LSEN 1u   /* sensor DVDD, selector in bits 11:7 */

/* Original KC02 H63P rail selectors and clock. */
#define AX_H63P_MCLK_HZ 24000000u
#define AX_H63P_ID_MCLK_HZ 16000000u   /* sensorInit uses 16 MHz for detection */
#define AX_H63P_DVDD 6u
#define AX_H63P_AVDD 16u

static inline void ax_cam_ldo_set(ax_u32 ldo, ax_u32 sel, int en)
{
    ax_u32 v = AX_CAM_READ(AX_CAM_SYS_LDO);
    sel &= 0x1fu;
    if (ldo == AX_LDO_HSEN) {
        if (en) {
            /* The stock code only takes the HV path when sel > 22. */
            v = (v & 0xffffffe0u) | 0x20u | sel;
        } else {
            v &= ~0x20u;
        }
    } else if (ldo == AX_LDO_LSEN) {
        if (en) {
            v = (v & 0xfffff07fu) | 0x1000u | (sel << 7);
        } else {
            v &= ~(0x1000u | (0x1fu << 7));
        }
    } else {
        return;   /* no other LDO exists; do not invent one */
    }
    AX_CAM_WRITE(AX_CAM_SYS_LDO, v);
}

static inline void ax_cam_mclk_set(ax_u32 hz)
{
    AX_CAM_WRITE(AX_CAM_MCLK_CTL, AX_CAM_READ(AX_CAM_MCLK_CTL) & ~0x04000000u);
    AX_CAM_WRITE(AX_CAM_MCLK_CTL, AX_CAM_READ(AX_CAM_MCLK_CTL) |  0x04000000u);
    AX_CAM_WRITE(AX_CAM_PMAPCON2, (AX_CAM_READ(AX_CAM_PMAPCON2) & ~(0x3u << 10)) | (2u << 10));
    AX_CAM_WRITE(AX_CAM_GPIO_MAP_PE, AX_CAM_READ(AX_CAM_GPIO_MAP_PE) | AX_CAM_PE10);
    AX_CAM_WRITE(AX_CAM_GPIO_DIR_PE, AX_CAM_READ(AX_CAM_GPIO_DIR_PE) & ~AX_CAM_PE10);
    AX_CAM_WRITE(AX_CAM_MCLK_EN, AX_CAM_READ(AX_CAM_MCLK_EN) & ~0x20u);
    /* Divider is a 5-bit field, so the mclk must stay above ~9 MHz. */
    ax_u32 div = (hz && hz <= 288000000u) ? (288000000u / hz - 1u) : 0u;
    AX_CAM_WRITE(AX_CAM_MCLK_DIV,
                 (AX_CAM_READ(AX_CAM_MCLK_DIV) & ~0x1f00u) | ((div & 0x1fu) << 8));
}

/* Stock order is MCLK -> IIC enable -> identify -> rails -> sensor MCLK.
 * Luma raises the rails first so a cold camera is powered before the ID read;
 * that ordering is strictly safer than the stock one and cannot hurt. */
static inline void ax_cam_power_h63p(void)
{
    ax_cam_ldo_set(AX_LDO_LSEN, AX_H63P_DVDD, 1);
    ax_cam_ldo_set(AX_LDO_HSEN, AX_H63P_AVDD, 1);
    ax_cam_mclk_set(AX_H63P_ID_MCLK_HZ);
}

#endif /* AX_CAM_POWER_H */
