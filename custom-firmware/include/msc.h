/* SPDX-License-Identifier: MIT
 * USB mass storage: Bulk-Only Transport with a SCSI transparent command set.
 *
 * Transport independent and non-blocking. The engine drives the link rather
 * than being driven by endpoint interrupts, because the AX3292 backend is
 * polled. `send` and `receive` report "not ready" instead of blocking, so a
 * slow host can never stall the monitor or the target loop.
 *
 * The medium is a raw block device, not a filesystem. Exporting it while the
 * local filesystem still has the card mounted is the caller's responsibility,
 * and the whole point of the ownership state in storage.c: one FAT, one writer.
 */
#ifndef LUMA_MSC_H
#define LUMA_MSC_H
#include "luma.h"
#define MSC_CBW_SIZE 31
#define MSC_CSW_SIZE 13
#define MSC_CBW_SIGNATURE 0x43425355u
#define MSC_CSW_SIGNATURE 0x53425355u
#define MSC_BLOCK 512u
typedef enum { MSC_PASSED = 0, MSC_FAILED = 1, MSC_PHASE_ERROR = 2 } MscStatus;
/* SCSI sense keys, the subset this device can produce. */
enum {
    MSC_SENSE_NONE = 0x00,
    MSC_SENSE_NOT_READY = 0x02,
    MSC_SENSE_MEDIUM_ERROR = 0x03,
    MSC_SENSE_ILLEGAL_REQUEST = 0x05,
    MSC_SENSE_UNIT_ATTENTION = 0x06,
    MSC_SENSE_DATA_PROTECT = 0x07,
};
typedef struct {
    void *context;
    u32 blocks;          /* 512-byte blocks; 0 means no medium is present. */
    int writable;
    /* Both are single-block and must be bounded by MSC_BLOCK. */
    int (*read)(void *context, u32 lba, u8 *out);
    int (*write)(void *context, u32 lba, const u8 *in);
    int (*sync)(void *context);
} MscMedium;
typedef struct {
    void *context;
    unsigned max_packet;
    /* 1 sent, 0 endpoint busy, -1 error. */
    int (*send)(void *context, const u8 *data, unsigned size);
    /* Bytes accepted, 0 nothing available yet, -1 error. */
    int (*receive)(void *context, u8 *out, unsigned size);
    void (*stall)(void *context, int in);
    void (*reset)(void *context);
} MscLink;
enum { MSC_PHASE_IDLE, MSC_PHASE_DATA, MSC_PHASE_CSW, MSC_PHASE_RESET };
typedef struct {
    MscMedium medium;
    MscLink link;
    unsigned phase;
    u32 tag;
    u8 cdb[16];
    unsigned cdb_len, expect, data_total, transferred, direction;
    unsigned lba, blocks, sector_offset;
    u8 sector[MSC_BLOCK];
    int sector_loaded;
    int inline_data;
    unsigned inline_len, inline_sent;
    u8 inline_buf[64];
    u8 csw[MSC_CSW_SIZE];
    unsigned csw_offset;
    MscStatus status;
    unsigned char sense_key, sense_asc, sense_ascq;
    int prevented;
    /* Set when the link cannot carry a transfer at all. A misconfigured link
     * must be refused, not called through a null pointer. */
    int fault;
    /* Diagnostics, read back by tests and the observation words. */
    unsigned commands, errors, stalls, resets, unit_attention;
} Msc;
void msc_init(Msc *m, const MscMedium *medium, const MscLink *link);
/* Accepts one CBW frame. Returns 1 when the command already finished, 0 when a
 * data phase is pending and msc_poll must be called, -1 when the frame was not
 * a usable CBW and both endpoints were stalled. */
int msc_cbw(Msc *m, const u8 *frame, unsigned size);
/* Advances the current data phase and the status phase. Returns 1 when the CSW
 * has been handed to the link, 0 when more polling is needed, -1 on a link
 * error. Safe to call in any phase. */
int msc_poll(Msc *m);
/* Bulk-Only Mass Storage Reset: aborts the current command and clears stalls. */
void msc_reset(Msc *m);
int msc_idle(const Msc *m);
#endif
