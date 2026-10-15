/* SPDX-License-Identifier: MIT
 * Binds a mass-storage LUN to the SD card under the storage ownership rules.
 *
 * A PC and the camera must never modify one FAT. So the LUN reports no medium
 * until ownership has been handed over, and while the host owns the card every
 * local filesystem call is refused. Taking ownership back invalidates the mount,
 * which forces a remount before the camera reads anything again.
 *
 * The block callbacks are injected so the ownership logic can be tested on a
 * host without the SD register layer.
 */
#ifndef LUMA_MSC_MEDIUM_H
#define LUMA_MSC_MEDIUM_H
#include "msc.h"
#include "storage.h"
typedef struct {
    Fat *volume;
    u32 capacity;           /* 512-byte blocks, from the card CSD */
    int (*block_read)(void *context, u32 lba, u8 *out);
    int (*block_write)(void *context, u32 lba, const u8 *in);
    void *context;
    unsigned exports, reclaims, refused;
} MscSd;
void msc_sd_init(MscSd *sd, Fat *volume, u32 capacity,
                 int (*read)(void *, u32, u8 *), int (*write)(void *, u32, const u8 *),
                 void *context);
/* Fills in the medium this LUN presents. `blocks` is zero until the handoff, so
 * an unready card is SCSI NOT READY rather than a fabricated disk. */
void msc_sd_medium(MscSd *sd, MscMedium *medium);
/* Hand the card to the host. Refused when the local side still has it mounted
 * with work outstanding, which is what `io_idle` reports. */
int msc_sd_export(MscSd *sd, int io_idle);
/* Take the card back. The local mount is invalidated and must be remounted. */
int msc_sd_reclaim(MscSd *sd, int io_idle);
int msc_sd_host_owns(const MscSd *sd);
#endif
