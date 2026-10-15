/* IIC1 read semantics + per-register restore diagnosis.
 *
 * INPUT[0] = 0 : read-only report, no writes.
 * INPUT[0] = 1 : enables IIC1 only, then runs four reads and finally reports
 *                every saved register individually so an inexact restore can be
 *                attributed instead of guessed at. All writes are restored.
 *
 * Context: IIC0 has no responder (NACK at address 0x40, transfers never
 * complete), while IIC1 ACKs and completes a read but returns 0xff. Stock's own
 * helper also reads 0xff on its bus, so the value is not a driver defect. This
 * probe asks whether 0xff is register-specific or universal, and whether the
 * read framing matters.
 *
 * OUTPUT: revision, mode, orig/after pairs for the 8 registers this probe may
 * touch, then status/value for four read attempts.
 */
#include "common.h"
#include "ax_sensor.h"

#define REG_PMAPCON2   0x8348u
#define REG_GMAP_PE    0x810cu
#define REG_GDIR_PE    0x804cu
#define REG_GDATA_PE   0x800cu
#define REG_GPLU_PE    0x808cu
#define REG_GDGL_PE    0x838cu
#define REG_IIC1_CON   0x90d0u
#define REG_IIC1_BAUD  0x90ccu

/* Read with a repeated START and no intervening STOP. */
static int read_repeated(ax_u32 base, ax_u8 slave, ax_u8 reg, ax_u8 *out)
{
    ax_iic_start_at(base);
    if (!ax_iic_send_at(base, slave) || !ax_iic_ack_at(base)) { ax_iic_stop_at(base); return 0; }
    if (!ax_iic_send_at(base, reg)   || !ax_iic_ack_at(base)) { ax_iic_stop_at(base); return 0; }
    ax_iic_start_at(base);                        /* repeated start */
    if (!ax_iic_send_at(base, (ax_u8)(slave | 1u)) || !ax_iic_ack_at(base)) {
        ax_iic_stop_at(base); return 0;
    }
    if (!ax_iic_recv_at(base, out)) { ax_iic_stop_at(base); return 0; }
    ax_iic_stop_at(base);
    return 1;
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
    u32 o_con1 = AX_SENSOR_READ(REG_IIC1_CON);
    u32 o_bd1  = AX_SENSOR_READ(REG_IIC1_BAUD);

    out[0] = 6;
    out[1] = in[0];
    out[2] = o_pmap;  out[3] = o_pmap;
    out[4] = o_dgl;   out[5] = o_dgl;
    out[6] = o_gmap;  out[7] = o_gmap;
    out[8] = o_gdir;  out[9] = o_gdir;
    out[10] = o_gdat; out[11] = o_gdat;
    out[12] = o_gplu; out[13] = o_gplu;
    out[14] = o_con1; out[15] = o_con1;
    out[16] = o_bd1;  out[17] = o_bd1;
    if (in[0] != 1u) { finish(buffer, length, parameter, 26); return; }

    ax_iic_enable_bus(1u, AX_IIC_BAUD_DIVISOR(200000u));

    ax_u8 v = 0;
    /* A: registry read using the driver's STOP-then-START framing. */
    out[18] = (u32)ax_sensor_read_reg_at(AX_IIC1_BASE, AX_SENSOR_SLAVE_ADDR, 0x0bu, &v);
    out[19] = v;
    /* B: same register, repeated START, no STOP in between. */
    v = 0;
    out[20] = (u32)read_repeated(AX_IIC1_BASE, AX_SENSOR_SLAVE_ADDR, 0x0bu, &v);
    out[21] = v;
    /* C and D: other registers, to see whether 0xff is universal. */
    v = 0;
    out[22] = (u32)read_repeated(AX_IIC1_BASE, AX_SENSOR_SLAVE_ADDR, 0x00u, &v);
    out[23] = v;
    v = 0;
    out[24] = (u32)read_repeated(AX_IIC1_BASE, AX_SENSOR_SLAVE_ADDR, 0xf0u, &v);
    out[25] = v;

    /* Per-register restore report, so any mismatch is attributed. */
    AX_SENSOR_WRITE(REG_IIC1_CON,  o_con1);
    AX_SENSOR_WRITE(REG_IIC1_BAUD, o_bd1);
    AX_SENSOR_WRITE(REG_PMAPCON2,  o_pmap);
    AX_SENSOR_WRITE(REG_GDGL_PE,   o_dgl);
    AX_SENSOR_WRITE(REG_GMAP_PE,   o_gmap);
    AX_SENSOR_WRITE(REG_GDIR_PE,   o_gdir);
    AX_SENSOR_WRITE(REG_GDATA_PE,  o_gdat);
    AX_SENSOR_WRITE(REG_GPLU_PE,   o_gplu);
    out[3]  = AX_SENSOR_READ(REG_PMAPCON2);
    out[5]  = AX_SENSOR_READ(REG_GDGL_PE);
    out[7]  = AX_SENSOR_READ(REG_GMAP_PE);
    out[9]  = AX_SENSOR_READ(REG_GDIR_PE);
    out[11] = AX_SENSOR_READ(REG_GDATA_PE);
    out[13] = AX_SENSOR_READ(REG_GPLU_PE);
    out[15] = AX_SENSOR_READ(REG_IIC1_CON);
    out[17] = AX_SENSOR_READ(REG_IIC1_BAUD);

    finish(buffer, length, parameter, 26);
}
