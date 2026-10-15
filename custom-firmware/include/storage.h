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
#define FAT_WRITE_MAX (512u*1024u)
int fat_mount(Fat *fs, SectorRead read, SectorWrite write, u32 capacity);
/* Bounded streaming writer, for files too large to hold in RAM.
 *
 * Allocation happens while the file being replaced is still marked in use, so a
 * free-cluster scan can never land inside the old chain and the new chain can
 * never overlap it. The directory entry is the single commit point: an abort
 * frees the new allocation and leaves the old file exactly as it was, so an
 * interrupted upload does not damage what is already on the card. */
typedef struct {
    Fat *volume;
    u8 name[11];
    u32 start, needed, written, size, old_start;
    unsigned slot, replacing, open, failed;
    unsigned sector_used;
    u8 sector[512];
} FatWrite;
int fat_write_begin(Fat *fs, const char *name, u32 size, FatWrite *w);
int fat_write_data(FatWrite *w, const u8 *bytes, unsigned size);
int fat_write_end(FatWrite *w);
void fat_write_abort(FatWrite *w);
/* Creates or replaces a root file in one call. */
int fat_replace(Fat *fs, const char *name, const u8 *bytes, unsigned size);
/* Frees a chain whose directory entry has already been published. */
int fat_free_chain(Fat *fs, u32 start);
/* Forward-only streaming reader for files too large to buffer. One 512-byte
 * staging sector plus one cached FAT sector; no allocation. */
typedef struct {
    Fat *fs;
    u32 size, done, current, in_cluster, lba;
    u8 sector[512];
    int cached;
} FileStream;
int fat_stream_open(Fat *fs, const char *name, FileStream *stream);
int fat_stream_read(FileStream *stream, u8 *out, unsigned size);
int fat_read(Fat *fs, const char *name, u8 *out, unsigned cap, unsigned *size);
int fat_create(Fat *fs, const char *name, const u8 *bytes, unsigned size);
unsigned fat_photos(Fat *fs, unsigned *ids, unsigned cap);
void photo_name(char out[13], unsigned id);
/* No handoff unless the camera has closed all file handles and DMA is idle. */
int sd_handoff(Fat *fs, SdOwner to, int io_idle);
#endif
