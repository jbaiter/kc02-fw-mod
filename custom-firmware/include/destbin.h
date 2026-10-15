/* SPDX-License-Identifier: MIT
 * Read-only legacy DestBin.bin inspection, original KC02 only.
 * This is NOT the A/B LUMAIMG format and contains no erase/program callback.
 */
#ifndef LUMA_DESTBIN_H
#define LUMA_DESTBIN_H
#include "update.h"
#define DESTBIN_SIZE 0x400000u
#define DESTBIN_BOOT_SIZE 0x2600u
#define DESTBIN_APP_SIZE (0x644u*512u)
typedef enum { DESTBIN_OK, DESTBIN_INVALID, DESTBIN_IO, DESTBIN_PROTECTED } DestBinResult;
typedef struct { u32 size, crc, app_offset, app_size; } DestBinInfo;
/* protected_source supplies the running boot/recovery prefix. Reject any
 * candidate that changes it. CRC is a fingerprint, NOT authentication.
 * No source executable is loaded or called by this function. */
DestBinResult destbin_inspect(const ImageSource *candidate,
                             const ImageSource *protected_source,
                             u32 protected_bytes, DestBinInfo *);
#endif
