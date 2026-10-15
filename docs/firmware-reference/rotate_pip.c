/* Stock KC02 firmware reference: the rotation engine and the pip counter.
 *
 * Decompiled from firmware/original.bin. The SDK has ax32xx/src/ax32xx_rotate.c
 * and hal/src/hal_rotate.c, but those files describe an engine whose BEHAVIOUR is
 * only clear once combined with what the hardware actually reads back, and the
 * measurements below are what a port needs.
 */
#include <stdint.h>

typedef uint8_t  u8;
typedef uint32_t u32;

#define REG(n) (*(volatile u32 *)(n))

/* ---------------------------------------------------------------------------
 * SFR1 registers, from ax32xx/inc/ax329x.h. SPRGROUP_SFR1 = 17 << 11 = 0x8800,
 * and each index is a word, so register = 0x8800 + index*4.
 * ------------------------------------------------------------------------ */
#define ROTATECON   0x898cu
#define ROTSRCYADR  0x8990u
#define ROTSRCUVADR 0x8994u
#define ROTTGYADR   0x8998u
#define ROTTGUVADR  0x899cu
#define ROTIMGSIZE  0x89a0u
#define ROTADDRSTRI 0x89a4u
#define ROTATESTA   0x89a8u

/* 0x0202ea98 area -- ax32xx_rotateStart, transcribed from the SDK body.
 *
 * ROTATECON has NO ANGLE FIELD. Bit 31 busy/kick, bit 2 int enable, bit 1 source
 * mode (0 video, 1 osd), bit 0 enable. The rotation direction is fixed in
 * hardware, so a port cannot choose it -- it must match the panel mounting.
 *                                                                     VERIFIED
 */
void ax32xx_rotateStart(u8 src_mode, u32 src_w, u32 src_h,
                        u8 *dst_y, u8 *dst_uv, u32 dst_stride,
                        u8 *src_y, u8 *src_uv, u32 src_stride,
                        u8 int_en)
{
    ax32xx_sysClkSet(SYS_CLK_ROTATE, 1);
    REG(ROTATECON)   = (0u << 31) | ((u32)int_en << 2) | ((u32)src_mode << 1) | 1u;
    REG(ROTATESTA)   = 1u;
    REG(ROTIMGSIZE)  = (((src_h & ~1u) - 1u) << 16) | ((src_w & ~1u) - 1u);
    REG(ROTSRCYADR)  = (u32)src_y  & ~0x3fu;
    REG(ROTSRCUVADR) = (u32)src_uv & ~0x3fu;
    REG(ROTTGYADR)   = (u32)dst_y  & ~0x1fu;
    REG(ROTTGUVADR)  = (u32)dst_uv & ~0x1fu;
    REG(ROTADDRSTRI) = ((dst_stride / 4u) << 16) | (src_stride / 4u);
    ax32xx_intEnable(IRQ_ROTATE, int_en);
    REG(ROTATECON) |= (1u << 31);                 /* kick */
}

int  ax32xx_rotateCheckBusy(void)   { return (int)(REG(ROTATECON) >> 31); }
u32  ax32xx_rotateGetSrcYAddr(void) { return REG(ROTSRCYADR) & 0x1ffffffu; }

/* ---------------------------------------------------------------------------
 * FIELD ENCODINGS, decoded and MEASURED while a stock preview was running:
 *
 *   ROTIMGSIZE  high 16 = src_height - 1, low 16 = src_width - 1
 *               measured 0x00b3013f -> src 320x180, i.e. the landscape CSI frame
 *   ROTADDRSTRI high 16 = dst_stride / 4 words, low 16 = src_stride / 4 words
 *               measured 0x00300050 -> dst 192 bytes, src 320 bytes
 *   ROTATECON   measured 5 = busy 0, int_en 1, mode video, enable 1
 *   ROTTGYADR   measured 0x0015a480, which EQUALS the DE video plane address:
 *               the rotator's destination IS the display plane
 *   ROTSRCYADR  measured a 24-bit offset into a CSI-format buffer
 *   ROTATESTA   0 at rest; a one-shot per frame, not free-running
 *                                                                     VERIFIED
 *
 * The pixels confirm it: the DE plane is a 90-degree rotation of the CSI plane,
 * correlation +0.9943 over two independent frame pairs, with the content bounding
 * box mapping exactly under that rotation, and the chroma rotating with it.
 *
 * CONSEQUENCE worth repeating: because the rotator's target is the DE plane, any
 * port that renders straight into that plane must DISABLE the engine
 * (ROTATECON = 0) or it will be quietly overwritten every frame.
 * ------------------------------------------------------------------------ */

/* ---------------------------------------------------------------------------
 * 0x020329d4  ax32xx_pipCounterReset(src)
 *
 * Small, and easy to omit -- which is exactly what happened when re-arming the
 * CSI LCD transfer. The stock CSI interrupt re-arms as CLEAR -> this -> SET, and
 * setting the bit alone does not restart a finished transfer.
 *
 * The condition is a source-select check: bit 1 of 0x8980 says which pip source
 * is current, and the reset is applied only when the request matches its inverse.
 *                                                                     VERIFIED
 * ------------------------------------------------------------------------ */
void ax32xx_pipCounterReset(u8 src)
{
    u32 v = REG(0x8980);
    if ((u32)src == (((v >> 1) & 1u) ^ 1u)) { REG(0x8980) = v | 4u; }
}
