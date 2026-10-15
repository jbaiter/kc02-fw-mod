/* SPDX-License-Identifier: MIT
 * Host-side BOT peer for testing the mass-storage engine.
 *
 * It acts as the USB host: it builds real CBWs, drains the bulk-IN pipe into a
 * data buffer, answers bulk-OUT requests from a data buffer, and parses the CSW
 * the device returns. That makes the tests a protocol conversation rather than
 * a set of calls into the engine's own helpers.
 *
 * Everything is reached through accessors, so the tests never depend on a
 * struct layout and cannot silently read the wrong field if the engine changes.
 */
#ifndef LUMA_MSC_PEER_H
#define LUMA_MSC_PEER_H
#include "msc.h"
#define PEER_DATA_MAX 8192
#define PEER_STALL_IN 1
#define PEER_STALL_OUT 0
typedef struct {
    Msc msc;
    u8 out[PEER_DATA_MAX];
    unsigned out_size;
    u8 in[PEER_DATA_MAX];
    unsigned in_size, in_at;
    u8 csw[MSC_CSW_SIZE];
    unsigned csw_size;
    unsigned stalls_in, stalls_out, resets;
    int stalled_in, stalled_out;
    u8 medium[PEER_DATA_MAX];
    u32 blocks;
    int writable;
    unsigned reads, writes, syncs;
    int fail_read, fail_write;
    u32 tag;
} Peer;
void peer_init(Peer *, u32 blocks, int writable);
/* One full command: feed the CBW, run the data phase, then report the CSW
 * status. Returns the CSW status, or -1 when the frame was rejected. */
int peer_command(Peer *, const u8 *cdb, unsigned cdb_len, unsigned data_len,
                 const u8 *data_in, unsigned data_in_size);
const u8 *peer_out(Peer *);          /* data the device sent, CSW excluded */
unsigned peer_out_size(Peer *);
const u8 *peer_medium(Peer *);
const u8 *peer_csw(Peer *);
u32 peer_csw32(Peer *, unsigned offset);
unsigned peer_residue(Peer *);
unsigned peer_reads(Peer *);
unsigned peer_writes(Peer *);
unsigned peer_syncs(Peer *);
unsigned peer_stalls(Peer *, int direction);
unsigned peer_resets(Peer *);
u32 peer_tag(Peer *);
void peer_fail_read(Peer *, int fail);
void peer_fail_write(Peer *, int fail);
Msc *peer_msc(Peer *);
/* Sends a raw frame straight at the engine, for malformed-CBW tests. */
int peer_raw_cbw(Peer *, const u8 *frame, unsigned size);
void peer_raw_reset(Peer *);
#endif
