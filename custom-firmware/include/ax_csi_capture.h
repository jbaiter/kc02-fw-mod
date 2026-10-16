/* SPDX-License-Identifier: MIT
 * CSI frame bounds. The DMA destinations and CPU snapshots are separate objects.
 * Bench builds reserve spill space for a full unscaled 1280x720 frame so a bad
 * scaler configuration can trip guards without reaching unrelated Luma state.
 */
#ifndef AX_CSI_CAPTURE_H
#define AX_CSI_CAPTURE_H

#define AX_CSI_Y_BYTES (320u * 180u)
#define AX_CSI_UV_BYTES (320u * 90u)
#define AX_CSI_GUARD_WORDS 8u
#define AX_CSI_GUARD 0xc51dc51du

typedef struct {
    unsigned char data[AX_CSI_Y_BYTES];
    unsigned int guard[AX_CSI_GUARD_WORDS];
#ifdef LUMA_BENCH_RUN
    unsigned char spill[1280u * 720u + 1280u - AX_CSI_Y_BYTES];
#endif
    unsigned int tail[AX_CSI_GUARD_WORDS];
} AxCsiY;

typedef struct {
    unsigned char data[AX_CSI_UV_BYTES];
    unsigned int guard[AX_CSI_GUARD_WORDS];
#ifdef LUMA_BENCH_RUN
    unsigned char spill[1280u * 360u + 1280u - AX_CSI_UV_BYTES];
#endif
    unsigned int tail[AX_CSI_GUARD_WORDS];
} AxCsiUv;

static inline void ax_csi_guards_init(AxCsiY *y, AxCsiUv *uv)
{
    unsigned i;
    for (i=0; i<AX_CSI_GUARD_WORDS; ++i) {
        y->guard[i] = y->tail[i] = AX_CSI_GUARD;
        uv->guard[i] = uv->tail[i] = AX_CSI_GUARD;
    }
}

/* Volatile reads are required: a peripheral, not the CPU, can change a guard. */
static inline unsigned ax_csi_capture_faults(const volatile AxCsiY *y,
                                             const volatile AxCsiUv *uv)
{
    unsigned i, faults=0;
    for (i=0; i<AX_CSI_GUARD_WORDS; ++i) {
        if (y->guard[i] != AX_CSI_GUARD) faults |= 1u;
        if (uv->guard[i] != AX_CSI_GUARD) faults |= 2u;
        if (y->tail[i] != AX_CSI_GUARD) faults |= 4u;
        if (uv->tail[i] != AX_CSI_GUARD) faults |= 8u;
    }
    return faults;
}
#endif
