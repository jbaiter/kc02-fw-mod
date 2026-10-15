/* SPDX-License-Identifier: MIT */
#include "msc_medium.h"
static int host_owns(const MscSd *sd)
{
    return sd->volume && sd->volume->owner == SD_HOST;
}
int msc_sd_host_owns(const MscSd *sd) { return host_owns(sd); }
static int medium_read(void *ctx, u32 lba, u8 *out)
{
    MscSd *sd=ctx;
    if (!host_owns(sd) || !sd->block_read) { sd->refused++; return 0; }
    if (lba>=sd->capacity) { sd->refused++; return 0; }
    return sd->block_read(sd->context,lba,out);
}
static int medium_write(void *ctx, u32 lba, const u8 *in)
{
    MscSd *sd=ctx;
    if (!host_owns(sd) || !sd->block_write) { sd->refused++; return 0; }
    if (lba>=sd->capacity) { sd->refused++; return 0; }
    return sd->block_write(sd->context,lba,in);
}
void msc_sd_init(MscSd *sd, Fat *volume, u32 capacity,
                 int (*read)(void *, u32, u8 *), int (*write)(void *, u32, const u8 *),
                 void *context)
{
    *sd=(MscSd){.volume=volume,.capacity=capacity,.block_read=read,
                .block_write=write,.context=context};
}
void msc_sd_medium(MscSd *sd, MscMedium *medium)
{
    *medium=(MscMedium){.context=sd,
                        /* Zero blocks until the host owns the card, so an
                         * unhanded card is NOT READY and not a blank disk. */
                        .blocks=host_owns(sd) ? sd->capacity : 0,
                        .writable=host_owns(sd),
                        .read=medium_read,.write=medium_write,.sync=0};
}
int msc_sd_export(MscSd *sd, int io_idle)
{
    if (!sd->volume || !sd->capacity || !sd->block_read) { return 0; }
    if (!sd_handoff(sd->volume,SD_HOST,io_idle)) { return 0; }
    sd->exports++;
    return 1;
}
int msc_sd_reclaim(MscSd *sd, int io_idle)
{
    if (!sd->volume || !sd_handoff(sd->volume,SD_LOCAL,io_idle)) { return 0; }
    sd->reclaims++;
    return 1;
}
