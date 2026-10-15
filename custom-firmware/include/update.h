/* SPDX-License-Identifier: MIT
 * Shared SD/WebUSB update engine. No physical NOR driver is supplied yet.
 * Only a verified recovery loader may enable it. Stock BLDR images are not
 * compatible with this proposed layout; initial installation is separate.
 */
#ifndef LUMA_UPDATE_H
#define LUMA_UPDATE_H
#include "luma.h"
#define NOR_SIZE 0x400000u
#define NOR_SECTOR 4096u
#define IMAGE_HEADER 64u
#define SLOT_SIZE 0x180000u
#define SLOT_A 0x020000u
#define SLOT_B 0x1a0000u
#define META_A 0x010000u
#define META_B 0x011000u
#define IMAGE_DEVELOPMENT 1u

typedef int (*ByteRead)(void *ctx, u32 offset, u8 *out, unsigned size);
typedef struct { void *context; ByteRead read; u32 size; } ImageSource;
typedef struct {
    void *context;
    ByteRead read;
    int (*erase)(void *ctx, u32 offset); /* Exactly one aligned 4 KiB sector. */
    int (*program)(void *ctx, u32 offset, const u8 *data, unsigned size);
} Nor;
typedef struct { u32 bytes, entry, version, crc, boards; } ImageInfo;
typedef struct { u32 generation, slot, bytes, crc, version; } BootRecord;
typedef enum { UPDATE_OK, UPDATE_DENIED, UPDATE_INVALID, UPDATE_IO,
               UPDATE_NO_BOOT, UPDATE_STALE } UpdateResult;
int image_validate(const ImageSource *source, u32 board_bit, ImageInfo *out);
int boot_select(const Nor *nor, BootRecord *record);
UpdateResult update_apply(const Nor *nor, const ImageSource *source, u32 board_bit,
                          int physical_confirmation, int recovery_verified);
#endif
