/* SPDX-License-Identifier: MIT
 * Read-only legacy DestBin.bin inspection, original KC02 only.
 * This is NOT the A/B LUMAIMG format and contains no erase/program callback.
 *
 * The two checks are separate on purpose. Structure/CRC needs only the
 * candidate; the protected-prefix comparison needs a trusted snapshot of the
 * running boot/recovery bytes, which a stage may not be able to obtain. A
 * caller that cannot supply one must report the prefix check as unavailable
 * rather than treating the candidate as verified.
 */
#ifndef LUMA_DESTBIN_H
#define LUMA_DESTBIN_H
#include "update.h"
#define DESTBIN_SIZE 0x400000u
#define DESTBIN_BOOT_SIZE 0x2600u
#define DESTBIN_APP_SIZE (0x644u*512u)
typedef enum { DESTBIN_OK, DESTBIN_INVALID, DESTBIN_IO, DESTBIN_PROTECTED } DestBinResult;
typedef struct { u32 size, crc, app_offset, app_size; } DestBinInfo;
/* Checks magic, additive checksum, boot signature, load address, region
 * fields, SFAT marker and the full-image CRC. Reads the whole candidate.
 * CRC is a fingerprint, NOT authentication. Nothing is loaded or executed. */
DestBinResult destbin_inspect(const ImageSource *candidate, DestBinInfo *info);
/* Compares the first protected_bytes of the candidate against a running
 * boot/recovery snapshot. Rejects any difference. */
DestBinResult destbin_prefix_matches(const ImageSource *candidate,
                                    const ImageSource *running, u32 protected_bytes);
#endif
