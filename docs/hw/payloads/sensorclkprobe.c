/* IIC0 clock-gate experiment, using Luma's own drivers.
 *
 * INPUT[0] = 0 : read-only state report, no writes.
 * INPUT[0] = 1 : power/mux/enable as before, then retry one transfer with the
 *                SYS_CLK_IIC0 gate bit cleared and again with it set, to bracket
 *                the polarity. All changed registers are restored.
 *
 * Why: with the bus fully configured (Ctrl bit 7 stuck at 0x80, baud 143, pin
 * group 2) STATUS bit 0 still never set, so every transfer timed out rather than
 * being NACKed. That points below the register layer. ax32xx_sysClkSet writes
 * 0x8840 for clock indices < 0x20, bit = index, and SYS_CLK_IIC0 is 14; it sets
 * the bit when en == 0 and clears it when en != 0, so the bit reads as a gate
 * whose polarity has to be measured, not assumed.
 *
 * OUTPUT: revision, mode, clock-gate registers, per-attempt status/ack/ID, restore.
 */
#include "common.h"
#include "ax_sensor.h"
#include "ax_cam_power.h"

#define REG_SYS_LDO    0x8868u
#define REG_MCLK_DIV   0x884cu
#define REG_MCLK_CTL   0x8850u
#define REG_MCLK_EN    0x8848u
#define REG_PMAPCON2   0x8348u
#define REG_GMAP_PE    0x810cu
#define REG_GDIR_PE    0x804cu
#define REG_IIC_CON    0x9094u
#define REG_IIC_BAUD   0x9090u
#define REG_CLK_LOW    0x8840u   /* clock gates, indices 0..31 */
#define REG_CLK_HIGH   0x8844u   /* clock gates, indices 32..63 */
#define AX_SYS_CLK_IIC0 14u

/* One addressed register read, capturing the bus mid-transfer. */
static u32 probe_id(volatile u32 *out, u32 base)
{
    ax_u8 id = 0;
    ax_iic_start_at(AX_IIC0_BASE);
    ax_iic_send_at(AX_IIC0_BASE, AX_SENSOR_SLAVE_ADDR);
    out[base + 0] = AX_CAM_READ(AX_IIC0_BASE + AX_IIC_OFF_STA);
    out[base + 1] = (u32)ax_iic_ack_at(AX_IIC0_BASE);
    ax_iic_send_at(AX_IIC0_BASE, AX_SENSOR_ID_REG);
    out[base + 2] = AX_CAM_READ(AX_IIC0_BASE + AX_IIC_OFF_STA);
    out[base + 3] = (u32)ax_iic_ack_at(AX_IIC0_BASE);
    ax_iic_stop_at(AX_IIC0_BASE);
    out[base + 4] = (u32)ax_sensor_read_reg(AX_SENSOR_SLAVE_ADDR, AX_SENSOR_ID_REG, &id);
    out[base + 5] = id;
    return id;
}

__attribute__((section(".entry")))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    u32 i;
    for (i = 0; i < 26; ++i) out[i] = 0;

    u32 o_clk  = AX_CAM_READ(REG_CLK_LOW);
    u32 o_con  = AX_CAM_READ(REG_IIC_CON);
    u32 o_baud = AX_CAM_READ(REG_IIC_BAUD);
    u32 o_pmap = AX_CAM_READ(REG_PMAPCON2);
    u32 o_ldo  = AX_CAM_READ(REG_SYS_LDO);
    u32 o_div  = AX_CAM_READ(REG_MCLK_DIV);
    u32 o_ctl  = AX_CAM_READ(REG_MCLK_CTL);
    u32 o_en   = AX_CAM_READ(REG_MCLK_EN);
    u32 o_gmap = AX_CAM_READ(REG_GMAP_PE);
    u32 o_gdir = AX_CAM_READ(REG_GDIR_PE);

    out[0] = 4;
    out[1] = in[0];
    out[2] = o_clk;
    out[3] = AX_CAM_READ(REG_CLK_HIGH);
    if (in[0] != 1u) { finish(buffer, length, parameter, 26); return; }

    ax_cam_ldo_set(AX_LDO_LSEN, AX_H63P_DVDD, 1);
    ax_cam_ldo_set(AX_LDO_HSEN, AX_H63P_AVDD, 1);
    ax_cam_mclk_set(AX_H63P_ID_MCLK_HZ);
    ax_iic_enable(AX_IIC_BAUD_DIVISOR(200000u));
    out[4] = AX_CAM_READ(REG_IIC_CON);

    /* Attempt A: gate left exactly as found. */
    (void)probe_id(out, 5);

    /* Attempt B: clear the SYS_CLK_IIC0 gate bit. */
    AX_CAM_WRITE(REG_CLK_LOW, AX_CAM_READ(REG_CLK_LOW) & ~(1u << AX_SYS_CLK_IIC0));
    out[11] = AX_CAM_READ(REG_CLK_LOW);
    (void)probe_id(out, 12);

    /* Attempt C: set it instead, in case the polarity is the other way round. */
    AX_CAM_WRITE(REG_CLK_LOW, AX_CAM_READ(REG_CLK_LOW) | (1u << AX_SYS_CLK_IIC0));
    out[18] = AX_CAM_READ(REG_CLK_LOW);
    (void)probe_id(out, 19);

    /* Restore everything. */
    AX_CAM_WRITE(REG_CLK_LOW, o_clk);
    AX_CAM_WRITE(REG_SYS_LDO, o_ldo);
    AX_CAM_WRITE(REG_MCLK_DIV, o_div);
    AX_CAM_WRITE(REG_MCLK_CTL, o_ctl);
    AX_CAM_WRITE(REG_MCLK_EN, o_en);
    AX_CAM_WRITE(REG_PMAPCON2, o_pmap);
    AX_CAM_WRITE(REG_GMAP_PE, o_gmap);
    AX_CAM_WRITE(REG_GDIR_PE, o_gdir);
    AX_CAM_WRITE(REG_IIC_CON, o_con);
    AX_CAM_WRITE(REG_IIC_BAUD, o_baud);

    out[25] = (AX_CAM_READ(REG_CLK_LOW) == o_clk &&
                   AX_CAM_READ(REG_SYS_LDO) == o_ldo &&
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
