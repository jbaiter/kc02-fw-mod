/* Which IIC bus is the imager actually on?
 *
 * INPUT[0] = 0 : read-only bus register report, no writes.
 * INPUT[0] = 1 : muxes and enables IIC0, runs one identity read, then does the
 *                same on IIC1, capturing bus status at each stage. Every
 *                register it changed is restored before returning.
 *
 * Rails and CSI MCLK are deliberately NOT touched: a read-only probe already
 * showed the H63P rails enabled (DVDD 6 / AVDD 16) and MCLK already at 24 MHz in
 * USB mode, so this probe only varies the bus.
 *
 * Why: with IIC0 fully configured (Ctrl bit 7 read back 0x80, baud divisor 143,
 * pin group 2) STATUS bit 0 never set, so every transfer timed out instead of
 * being NACKed, and the SYS_CLK_IIC0 gate was already open. The SDK defines two
 * IIC buses and the emulator models both, so the bus itself is what to question.
 *
 * OUTPUT: revision, mode, original bus registers, per-bus status/ack/ID, restore.
 */
#include "common.h"
#include "ax_sensor.h"

#define REG_PMAPCON2   0x8348u
#define REG_GMAP_PE    0x810cu
#define REG_GDIR_PE    0x804cu
#define REG_GDATA_PE   0x800cu
#define REG_GPLU_PE    0x808cu
#define REG_GDGL_PE    0x838cu
#define REG_IIC0_CON   0x9094u
#define REG_IIC0_BAUD  0x9090u
#define REG_IIC1_CON   0x90d0u
#define REG_IIC1_BAUD  0x90ccu

/* One addressed read on an explicit bus base. Records the bus after the address
 * byte so a timeout can be told apart from an addressed NACK. */
static void bus_probe(ax_u32 base, volatile u32 *out, u32 idx)
{
    ax_u8 id = 0;
    ax_iic_start_at(base);
    ax_iic_send_at(base, AX_SENSOR_SLAVE_ADDR);
    out[idx + 1] = AX_SENSOR_READ(base + AX_IIC_OFF_STA);
    out[idx + 2] = (u32)ax_iic_ack_at(base);
    ax_iic_stop_at(base);
    out[idx + 3] = (u32)ax_sensor_read_reg_at(base, AX_SENSOR_SLAVE_ADDR,
                                              AX_SENSOR_ID_REG, &id);
    out[idx + 4] = id;
    out[idx + 5] = AX_SENSOR_READ(base + AX_IIC_OFF_CON);
}

__attribute__((section(".entry")))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    u32 i;
    for (i = 0; i < 26; ++i) out[i] = 0;

    u32 o_pmap = AX_SENSOR_READ(REG_PMAPCON2);
    u32 o_dgl  = AX_SENSOR_READ(REG_GDGL_PE);
    u32 o_gmap = AX_SENSOR_READ(REG_GMAP_PE);
    u32 o_gdir = AX_SENSOR_READ(REG_GDIR_PE);
    u32 o_gdat = AX_SENSOR_READ(REG_GDATA_PE);
    u32 o_gplu = AX_SENSOR_READ(REG_GPLU_PE);
    u32 o_con0 = AX_SENSOR_READ(REG_IIC0_CON);
    u32 o_bd0  = AX_SENSOR_READ(REG_IIC0_BAUD);
    u32 o_con1 = AX_SENSOR_READ(REG_IIC1_CON);
    u32 o_bd1  = AX_SENSOR_READ(REG_IIC1_BAUD);

    out[0] = 5;
    out[1] = in[0];
    out[2] = o_pmap;  out[3] = o_con0;  out[4] = o_bd0;
    out[5] = o_con1;  out[6] = o_bd1;
    out[7] = o_dgl;   out[8] = o_gmap;  out[9] = o_gdir;
    out[10] = o_gdat; out[11] = o_gplu;
    if (in[0] != 1u) { finish(buffer, length, parameter, 26); return; }

    /* Bus 0 (IIC0): PMAPCON2 bits 2:0, SCL PE13 / SDA PE15. */
    ax_iic_enable_bus(0u, AX_IIC_BAUD_DIVISOR(200000u));
    bus_probe(AX_IIC0_BASE, out, 12);

    /* Bus 1 (IIC1): PMAPCON2 bits 5:3, SCL PE0 / SDA PE1. */
    ax_iic_enable_bus(1u, AX_IIC_BAUD_DIVISOR(200000u));
    bus_probe(AX_IIC1_BASE, out, 18);

    AX_SENSOR_WRITE(REG_IIC1_CON,  o_con1);
    AX_SENSOR_WRITE(REG_IIC1_BAUD, o_bd1);
    AX_SENSOR_WRITE(REG_IIC0_CON,  o_con0);
    AX_SENSOR_WRITE(REG_IIC0_BAUD, o_bd0);
    AX_SENSOR_WRITE(REG_PMAPCON2,  o_pmap);
    AX_SENSOR_WRITE(REG_GDGL_PE,   o_dgl);
    AX_SENSOR_WRITE(REG_GMAP_PE,   o_gmap);
    AX_SENSOR_WRITE(REG_GDIR_PE,   o_gdir);
    AX_SENSOR_WRITE(REG_GDATA_PE,  o_gdat);
    AX_SENSOR_WRITE(REG_GPLU_PE,   o_gplu);

    out[24] = (AX_SENSOR_READ(REG_PMAPCON2)  == o_pmap &&
               AX_SENSOR_READ(REG_IIC0_CON)  == o_con0 &&
               AX_SENSOR_READ(REG_IIC1_CON)  == o_con1 &&
               AX_SENSOR_READ(REG_GDGL_PE)   == o_dgl  &&
               AX_SENSOR_READ(REG_GMAP_PE)   == o_gmap &&
               AX_SENSOR_READ(REG_GDIR_PE)   == o_gdir &&
               AX_SENSOR_READ(REG_GDATA_PE)  == o_gdat &&
               AX_SENSOR_READ(REG_GPLU_PE)   == o_gplu &&
               AX_SENSOR_READ(REG_IIC0_BAUD) == o_bd0  &&
               AX_SENSOR_READ(REG_IIC1_BAUD) == o_bd1) ? 1u : 0u;
    out[25] = AX_SENSOR_READ(REG_PMAPCON2);

    finish(buffer, length, parameter, 26);
}
