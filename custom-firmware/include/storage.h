/* SPDX-License-Identifier: MIT */
#ifndef LUMA_STORAGE_H
#define LUMA_STORAGE_H
#include "luma.h"
typedef int (*SectorRead)(u32 sector, u8 data[512]);
typedef int (*SectorWrite)(u32 sector, const u8 data[512]);
typedef enum { SD_LOCAL, SD_HOST, SD_UPDATE } SdOwner;
typedef struct {
    SectorRead read;
    SectorWrite write;
    u32 capacity, fat, root, data, total, clusters;
    u16 fat_sectors, root_entries;
    u8 spc, fats;
    int mounted, writable;
    SdOwner owner;
} Fat;
int fat_mount(Fat *fs, SectorRead read, SectorWrite write, u32 capacity);
int fat_read(Fat *fs, const char *name, u8 *out, unsigned cap, unsigned *size);
int fat_create(Fat *fs, const char *name, const u8 *bytes, unsigned size);
unsigned fat_photos(Fat *fs, unsigned *ids, unsigned cap);
void photo_name(char out[13], unsigned id);
/* No handoff unless the camera has closed all file handles and DMA is idle. */
int sd_handoff(Fat *fs, SdOwner to, int io_idle);
#endif
