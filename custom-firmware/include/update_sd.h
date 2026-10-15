/* SPDX-License-Identifier: MIT
 * SD card update path: find FIRMWARE.IMG in the card root, validate it, and
 * hand it to the shared update engine.
 *
 * This is the field-update mechanism, so the rules are strict:
 *
 *   - The card must be owned locally. Running an update while a PC owns the
 *     card would have two writers on one FAT, and the PC's view of the file
 *     could change underneath the write.
 *   - The file is validated in full before anything is mutated.
 *   - A validated file with no NOR backend is reported as such, not as a
 *     refusal and not as a success. There is no writer here yet.
 *   - The engine still requires physical confirmation and a verified recovery
 *     gate. This adapter cannot supply either on its own.
 *
 * The reader is a bounded streaming source, so a 1.5 MiB image costs no 1.5 MiB
 * buffer. The engine reads its source in two ascending passes, and a rewind
 * reopens the file rather than pretending to seek.
 */
#ifndef LUMA_UPDATE_SD_H
#define LUMA_UPDATE_SD_H
#include "update.h"
#include "storage.h"
#define UPDATE_SD_NAME "FIRMWARE.IMG"
typedef enum {
    /* Found and valid, and nothing has been written. Distinct from OK so that
     * a check can never be mistaken for an installation. */
    UPDATE_SD_READY,
    UPDATE_SD_OK,          /* Committed. */
    UPDATE_SD_NO_CARD,     /* No volume mounted. */
    UPDATE_SD_BUSY,        /* The host owns the card; not ours to read. */
    UPDATE_SD_NO_FILE,     /* No FIRMWARE.IMG in the root. */
    UPDATE_SD_BAD_FILE,    /* Size, header, board or CRC rejected. */
    UPDATE_SD_NO_WRITER,   /* Validated, but there is no NOR backend. */
    UPDATE_SD_DENIED,      /* No physical confirmation or no recovery gate. */
    UPDATE_SD_STALE,       /* Older than what is already installed. */
    UPDATE_SD_NO_BOOT,     /* Neither slot holds a bootable record. */
    UPDATE_SD_IO,          /* Read failure, or a failed erase/program/verify. */
} UpdateSdResult;
typedef struct {
    UpdateSdResult result;
    ImageInfo info;
    u32 file_bytes;
    int validated;         /* The image itself passed every check. */
} UpdateSdReport;
/* Discovery and validation only. Reads the whole file, mutates nothing. This is
 * what a UI offers for confirmation: version, board mask and size. */
UpdateSdResult update_sd_check(Fat *volume, u32 board_bit, UpdateSdReport *report);
/* Validation, then the shared engine. `nor` may be NULL, which reports
 * NO_WRITER after the file itself has been validated. */
UpdateSdResult update_sd_apply(Fat *volume, const Nor *nor, u32 board_bit,
                               int physical_confirmation, int recovery_verified,
                               UpdateSdReport *report);
#endif
