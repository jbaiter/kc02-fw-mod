/* SPDX-License-Identifier: MIT
 * Recovery gate: find and validate a stock-format update candidate on the SD
 * card, read-only. There is no writer here on purpose.
 *
 * The ordering matters. A recovering stage must establish that the card, the
 * filesystem and the candidate are intact before anything can be offered for
 * installation. Every outcome is distinct and terminal, so an operator sees
 * which check failed instead of a boot loop or a vague failure.
 */
#ifndef LUMA_RECOVER_H
#define LUMA_RECOVER_H
#include "storage.h"
#include "destbin.h"
typedef enum {
    RECOVER_OK,                  /* Candidate validated. No writer exists yet. */
    RECOVER_NO_CARD,             /* No card, or it did not answer as SDHC. */
    RECOVER_NO_FILESYSTEM,       /* Card present, no usable FAT16 volume. */
    RECOVER_NO_CANDIDATE,        /* No DESTBIN.BIN in the root directory. */
    RECOVER_CANDIDATE_IO,        /* The file could not be read completely. */
    RECOVER_CANDIDATE_INVALID,   /* Structure, size or CRC rejected. */
    RECOVER_PREFIX_UNAVAILABLE,  /* Structure is valid; boot prefix unverifiable. */
} RecoverResult;
typedef struct {
    RecoverResult result;
    DestBinInfo info;
    u32 capacity;
} RecoverReport;
/* Card bring-up stays with the caller, which owns the hardware. This mounts the
 * volume read-only and validates the candidate. */
RecoverResult recover_check(SectorRead read, u32 capacity, RecoverReport *report);
#endif
