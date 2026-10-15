/* Sensor power + SCCB identity probe, using Luma's own drivers.
 *
 * INPUT[0] = 0 : read-only. Reports the live power/clock/bus register state and
 *                performs no writes at all.
 * INPUT[0] = 1 : applies the H63P power sequence, enables the bus, and steps a
 *                single manual transfer while capturing bus state at each stage.
 *                Every register it changed is restored before returning.
 *
 * Context: the original sensorprobe called ax32xx_sysLDOSet with index 6 and
 * en=0. The stock LDO indices stop at 5, so that call hit the default branch and
 * did nothing. Register readback then showed the rails and MCLK were already
 * correct for the H63P in USB mode, and that IIC0_CON read 0, so the bus was the
 * remaining variable. This probe captures the bus registers per step because
 * ax_sensor_read_reg collapses timeout and NACK into one failure code.
 *
 * OUTPUT: revision, mode, 9 original values, powered state, and per-step bus
 * registers for one transfer, then the ID read and the restore check.
 */
#include "common.h"
#include "ax_sensor.h"
#include "ax_cam_power.h"

#define REG_SYS_LDO   0x8868u
#define REG_MCLK_DIV  0x884cu
#define REG_MCLK_CTL  0x8850u
#define REG_MCLK_EN   0x8848u
#define REG_PMAPCON2  0x8348u
#define REG_GMAP_PE   0x810cu
#define REG_GDIR_PE   0x804cu
#define REG_IIC_CON   0x9094u
#define REG_IIC_BAUD  0x9090u

__attribute__((section(".entry")))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    u32 i;
    for (i = 0; i < 26; ++i) out[i] = 0;

    u32 o_ldo  = AX_CAM_READ(REG_SYS_LDO);
    u32 o_div  = AX_CAM_READ(REG_MCLK_DIV);
    u32 o_ctl  = AX_CAM_READ(REG_MCLK_CTL);
    u32 o_en   = AX_CAM_READ(REG_MCLK_EN);
    u32 o_pmap = AX_CAM_READ(REG_PMAPCON2);
    u32 o_gmap = AX_CAM_READ(REG_GMAP_PE);
    u32 o_gdir = AX_CAM_READ(REG_GDIR_PE);
    u32 o_con  = AX_CAM_READ(REG_IIC_CON);
    u32 o_baud = AX_CAM_READ(REG_IIC_BAUD);

    out[0] = 3;                      /* probe revision */
    out[1] = in[0];
    out[2] = o_ldo;    out[3] = o_div;   out[4] = o_ctl;
    out[5] = o_en;     out[6] = o_pmap;  out[7] = o_gmap;
    out[8] = o_gdir;   out[9] = o_con;   out[10] = o_baud;

    if (in[0] != 1u) {
        finish(buffer, length, parameter, 26);
        return;
    }

    /* Rails, clock and pin mux, then the controller itself. */
    ax_cam_ldo_set(AX_LDO_LSEN, AX_H63P_DVDD, 1);
    ax_cam_ldo_set(AX_LDO_HSEN, AX_H63P_AVDD, 1);
    ax_cam_mclk_set(AX_H63P_ID_MCLK_HZ);       /* 16 MHz for the ID check */
    ax_iic_enable(AX_IIC_BAUD_DIVISOR(200000u));   /* muxes pins, then enables */

    out[11] = AX_CAM_READ(REG_SYS_LDO);
    out[12] = AX_CAM_READ(REG_MCLK_DIV);
    out[15] = AX_CAM_READ(REG_IIC_CON);       /* did bit 7 stick? */
    out[16] = AX_CAM_READ(REG_IIC_BAUD);      /* did the divisor stick? */
    out[25] = AX_CAM_READ(REG_PMAPCON2);      /* did the IIC0 group select stick? */

    /* One manual transfer, capturing the bus after each stage, so a timeout can
     * be told apart from an addressed NACK. */
    ax_iic_start_at(AX_IIC0_BASE);
    out[17] = AX_CAM_READ(AX_IIC0_BASE + AX_IIC_OFF_STA);
    ax_iic_send_at(AX_IIC0_BASE, AX_SENSOR_SLAVE_ADDR);
    out[18] = AX_CAM_READ(AX_IIC0_BASE + AX_IIC_OFF_STA);
    out[19] = (u32)ax_iic_ack_at(AX_IIC0_BASE);
    ax_iic_send_at(AX_IIC0_BASE, AX_SENSOR_ID_REG);
    out[20] = AX_CAM_READ(AX_IIC0_BASE + AX_IIC_OFF_STA);
    out[21] = (u32)ax_iic_ack_at(AX_IIC0_BASE);
    ax_iic_stop_at(AX_IIC0_BASE);

    /* Full identity read, at the ID clock and at the sensor's running clock. */
    {
        ax_u8 id = 0;
        out[13] = (u32)ax_sensor_read_reg(AX_SENSOR_SLAVE_ADDR, AX_SENSOR_ID_REG, &id);
        out[14] = id;
    }
    ax_cam_mclk_set(AX_H63P_MCLK_HZ);
    {
        ax_u8 id = 0;
        out[22] = (u32)ax_sensor_read_reg(AX_SENSOR_SLAVE_ADDR, AX_SENSOR_ID_REG, &id);
        out[23] = id;
    }

    /* Put every register back exactly as found. */
    AX_CAM_WRITE(REG_SYS_LDO, o_ldo);
    AX_CAM_WRITE(REG_MCLK_DIV, o_div);
    AX_CAM_WRITE(REG_MCLK_CTL, o_ctl);
    AX_CAM_WRITE(REG_MCLK_EN, o_en);
    AX_CAM_WRITE(REG_PMAPCON2, o_pmap);
    AX_CAM_WRITE(REG_GMAP_PE, o_gmap);
    AX_CAM_WRITE(REG_GDIR_PE, o_gdir);
    AX_CAM_WRITE(REG_IIC_CON, o_con);
    AX_CAM_WRITE(REG_IIC_BAUD, o_baud);

    out[24] = (AX_CAM_READ(REG_SYS_LDO) == o_ldo &&
               AX_CAM_READ(REG_MCLK_DIV) == o_div &&
               AX_CAM_READ(REG_MCLK_CTL) == o_ctl &&
               AX_CAM_READ(REG_MCLK_EN) == o_en &&
               AX_CAM_READ(REG_PMAPCON2) == o_pmap &&
               AX_CAM_READ(REG_GMAP_PE) == o_gmap &&
               AX_CAM_READ(REG_GDIR_PE) == o_gdir &&
               AX_CAM_READ(REG_IIC_CON) == o_con &&
               AX_CAM_READ(REG_IIC_BAUD) == o_baud) ? 1u : 0u;

    finish(buffer, length, parameter, 26);
}
