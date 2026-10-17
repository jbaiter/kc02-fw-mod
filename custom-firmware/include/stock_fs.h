/* SPDX-License-Identifier: MIT
 * Bounded adapter for the researched KC02 FatFs ABI. No formatting, unlink,
 * rename, update-image or arbitrary write-path API. Callbacks are injectable
 * for native tests; hardware must pin them and verify the SD routing first.
 */
#ifndef LUMA_STOCK_FS_H
#define LUMA_STOCK_FS_H
#include "luma.h"
#define STOCK_FS_OBJECT_WORDS 256u
#define STOCK_FS_OBJECT_GUARD 0xf17ef17eu
#define STOCK_FS_CHUNK 512u

typedef struct {
    u32 words[STOCK_FS_OBJECT_WORDS];
    u32 guard[8];
} StockFsObject;

typedef struct {
    int (*route_ok)(void);
    int (*open)(void *file, const char *path, u8 mode);
    int (*read)(void *file, void *out, unsigned want, unsigned *done);
    int (*write)(void *file, const void *in, unsigned want, unsigned *done);
    int (*close)(void *file);
    int (*opendir)(void *dir, const char *path);
    int (*readdir)(void *dir, void *info);
    int (*closedir)(void *dir);
    int (*stop)(void);
} StockFsOps;

enum { STOCK_FS_NONE=0, STOCK_FS_SAVED=1, STOCK_FS_LOADED=2, STOCK_FS_FAILED=-1 };

typedef struct {
    const StockFsOps *ops;
    StockFsObject file;
    unsigned ids[GALLERY_MAX], sizes[GALLERY_MAX], count, next_id;
    unsigned phase, kind, at, size, active_index, saved_id, loaded_size;
    int ready, writable, opened, last_error, pending_load;
    const u8 *save_bytes;
    u8 *load_bytes, *pending_bytes;
    unsigned pending_capacity;
    char path[16]; /* "0:/LUM00001.BMP" + NUL */
} StockFs;

/* Read-only scan of root LUMnnnnn.BMP files. Requires a verified SD route. */
int stock_fs_init(StockFs *fs, const StockFsOps *ops, int allow_writes);
/* Queue only: these return before any file I/O. Encoded save bytes must stay
 * immutable until the completion event. Read destinations must stay allocated. */
int stock_fs_save(StockFs *fs, const u8 *bytes, unsigned size);
int stock_fs_load(StockFs *fs, unsigned index, u8 *bytes, unsigned capacity);
/* One small I/O step; call between input polls and display service. A save is
 * reported only after close/SD-stop and a complete byte-for-byte readback. */
int stock_fs_step(StockFs *fs);
#endif
