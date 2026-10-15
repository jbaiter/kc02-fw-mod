/* SPDX-License-Identifier: MIT
 * AX3292 SDHC command/DMA contract, shared by the application HAL and the
 * bring-up recovery stage. Register map: vendor SDK ax32xx_sd.h and the
 * emulator's tools/emu/qemu/ax3292/ax3292-sd.h.
 *
 * Read-only or read/write use is the caller's choice; this layer has no
 * filesystem policy, no ownership state and no retry loop.
 */
#ifndef LUMA_SD_HW_H
#define LUMA_SD_HW_H
#include "luma.h"
#define SD_HW_SECTOR 512u
typedef struct {
    u8 *dma;        /* Caller-owned, 32-byte aligned, SD_HW_SECTOR bytes. */
    /* Vendor CT_CSD arithmetic (`((csize+1)<<10)`, see ax32xx hal_sd.c). The
     * result is numerically the card's 512-byte sector count, NOT a byte count;
     * u32 cannot hold a real byte count for cards above 4 GB. Callers compare
     * it against sector offsets, as fat_mount does. 0 until initialized. */
    u32 capacity;
    int ready;
} SdHw;
/* Brings the card up and reads CID/CSD from it. Returns the vendor capacity
 * value (sector count, see above), or 0 when there is no card, it is not SDHC,
 * or a command failed. A 0 result leaves the device unusable until a
 * successful re-init. */
u32 sd_hw_init(SdHw *sd, u8 *dma);
int sd_hw_read(SdHw *sd, u32 lba, u8 *out);
int sd_hw_write(SdHw *sd, u32 lba, const u8 *in);
#endif
