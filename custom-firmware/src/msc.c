/* SPDX-License-Identifier: MIT */
#include "msc.h"
#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_REQUEST_SENSE 0x03
#define SCSI_INQUIRY 0x12
#define SCSI_MODE_SENSE6 0x1a
#define SCSI_START_STOP 0x1b
#define SCSI_PREVENT_ALLOW 0x1e
#define SCSI_READ_CAPACITY10 0x25
#define SCSI_READ10 0x28
#define SCSI_WRITE10 0x2a
#define SCSI_SYNCHRONIZE10 0x35
#define SCSI_MODE_SENSE10 0x5a
#define SCSI_READ_CAPACITY16 0x9e
#define SCSI_REPORT_LUNS 0xa0
#define SCSI_READ12 0xa8
#define SCSI_WRITE12 0xaa
#define ASC_MEDIUM_NOT_PRESENT 0x3a
#define ASC_LBA_OUT_OF_RANGE 0x21
#define ASC_WRITE_PROTECTED 0x27
#define ASC_INVALID_COMMAND 0x20
#define ASC_INVALID_FIELD 0x24
#define ASC_RESET_OCCURRED 0x29
#define ASC_READ_ERROR 0x11
#define ASC_WRITE_ERROR 0x0c
#define ASC_MEDIUM_REMOVAL 0x53
/* BOT headers are little-endian; SCSI CDB fields are big-endian. Mixing them
 * up sends every transfer to the wrong block, so the two are named apart. */
static u32 le32(const u8 *p)
{
    return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);
}
static unsigned be16(const u8 *p) { return ((unsigned)p[0]<<8)|p[1]; }
static u32 be32(const u8 *p)
{
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static void set32(u8 *p, u32 v)
{
    p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24);
}
static void put32be(u8 *p, u32 n)
{
    p[0]=(u8)(n>>24); p[1]=(u8)(n>>16); p[2]=(u8)(n>>8); p[3]=(u8)n;
}
static void copy(u8 *out, const char *in, unsigned n)
{
    unsigned i;
    for (i=0; i<n; i++) { out[i]=(u8)in[i]; }
}
/* The first sense of an exchange survives, so a specific failure is not
 * overwritten by a later generic one. */
static void sense(Msc *m, unsigned char key, unsigned char asc)
{
    if (m->sense_key==MSC_SENSE_NONE) { m->sense_key=key; m->sense_asc=asc; m->sense_ascq=0; }
}
static void fail(Msc *m, unsigned char key, unsigned char asc)
{
    sense(m,key,asc);
    m->status=MSC_FAILED;
    m->data_total=0;
    /* `expect` is set by the caller to what the host asked for, so the CSW can
     * report the whole of it as residue. */
}
static void stall_all(Msc *m)
{
    m->stalls++;
    if (m->link.stall) {
        m->link.stall(m->link.context,0);
        m->link.stall(m->link.context,1);
    }
    m->phase=MSC_PHASE_RESET;
}
static void prepare_csw(Msc *m)
{
    set32(m->csw,MSC_CSW_SIGNATURE);
    set32(m->csw+4,m->tag);
    /* Residue is the difference between what the command asked for and what
     * was moved, whether or not the command succeeded. Reporting zero on a
     * short read tells the host it got data it never received. */
    set32(m->csw+8,m->expect-m->transferred);
    m->csw[12]=(u8)m->status;
    m->csw_offset=0;
    m->phase=MSC_PHASE_CSW;
}
static void begin_data(Msc *m, unsigned direction)
{
    m->direction=direction;
    m->transferred=0;
    m->sector_offset=0;
    m->sector_loaded=0;
    m->phase=MSC_PHASE_DATA;
}
static int medium_present(const Msc *m) { return m->medium.blocks && m->medium.read; }
static void fill_inline(Msc *m, unsigned length)
{
    m->inline_data=1;
    m->inline_len=length;
    m->inline_sent=0;
    m->expect=length;
    m->data_total=length;
}
/* Bounds one medium transfer. Returns 0 when the command was failed instead. */
static int set_medium_phase(Msc *m, u32 lba, unsigned blocks, int writing)
{
    u32 bytes;
    if (!medium_present(m)) { fail(m,MSC_SENSE_NOT_READY,ASC_MEDIUM_NOT_PRESENT); return 0; }
    if (blocks > m->medium.blocks || lba > m->medium.blocks-blocks) {
        fail(m,MSC_SENSE_ILLEGAL_REQUEST,ASC_LBA_OUT_OF_RANGE);
        return 0;
    }
    if (writing && (!m->medium.writable || !m->medium.write)) {
        fail(m,MSC_SENSE_DATA_PROTECT,ASC_WRITE_PROTECTED);
        return 0;
    }
    bytes=blocks*MSC_BLOCK;
    m->lba=lba;
    m->blocks=blocks;
    m->expect=bytes;
    m->data_total=bytes;
    return 1;
}
static void scsi_command(Msc *m, const u8 *cdb)
{
    u8 *out=m->inline_buf;
    unsigned i;
    for (i=0; i<sizeof(m->inline_buf); i++) { out[i]=0; }
    m->inline_data=0;
    m->inline_len=m->inline_sent=0;
    m->expect=m->data_total=m->transferred=0;
    m->direction=1;
    m->status=MSC_PASSED;
    /* A reset leaves one UNIT ATTENTION for the next command that is not
     * asking for sense, which is what a host expects to see and clear. */
    if (m->unit_attention && cdb[0]!=SCSI_REQUEST_SENSE && cdb[0]!=SCSI_INQUIRY) {
        m->unit_attention=0;
        fail(m,MSC_SENSE_UNIT_ATTENTION,ASC_RESET_OCCURRED);
        return;
    }
    switch (cdb[0]) {
    case SCSI_TEST_UNIT_READY:
        if (!medium_present(m)) { fail(m,MSC_SENSE_NOT_READY,ASC_MEDIUM_NOT_PRESENT); }
        return;
    case SCSI_REQUEST_SENSE: {
        unsigned want=cdb[4];
        if (want>18) { want=18; }
        out[0]=0x70;
        out[2]=(u8)m->sense_key;
        out[7]=10;
        out[12]=m->sense_asc;
        out[13]=m->sense_ascq;
        m->sense_key=MSC_SENSE_NONE; m->sense_asc=m->sense_ascq=0;
        fill_inline(m,want);
        return;
    }
    case SCSI_INQUIRY: {
        unsigned want;
        if (cdb[1]&1) { fail(m,MSC_SENSE_ILLEGAL_REQUEST,ASC_INVALID_FIELD); return; }
        want=cdb[4];
        if (want>36) { want=36; }
        out[0]=0x00;   /* direct access block device */
        out[1]=0x00;   /* not removable: a card in this device is not hot-swap */
        out[2]=0x05;   /* SPC-3 */
        out[3]=0x02;
        out[4]=31;
        copy(out+8,"LUMA    ",8);
        copy(out+16,"KC02 SD CARD    ",16);
        copy(out+32,"0001",4);
        fill_inline(m,want);
        return;
    }
    case SCSI_MODE_SENSE6: {
        unsigned want=cdb[4];
        out[0]=3; out[1]=0; out[2]=(u8)(cdb[2]&0x80); out[3]=0;
        fill_inline(m,4);
        if (want<4) { m->inline_len=want; m->expect=want; m->data_total=want; }
        return;
    }
    case SCSI_MODE_SENSE10: {
        unsigned want=be16(cdb+7);
        out[1]=6; out[2]=(u8)(cdb[2]&0x80);
        fill_inline(m,8);
        if (want<8) { m->inline_len=want; m->expect=want; m->data_total=want; }
        return;
    }
    case SCSI_REPORT_LUNS:
        put32be(out,16);
        fill_inline(m,16);
        return;
    case SCSI_READ_CAPACITY10:
        if (!medium_present(m)) { fail(m,MSC_SENSE_NOT_READY,ASC_MEDIUM_NOT_PRESENT); return; }
        put32be(out,m->medium.blocks-1);
        put32be(out+4,MSC_BLOCK);
        fill_inline(m,8);
        return;
    case SCSI_START_STOP:
        /* A host must not be able to eject media this device owns. */
        if (cdb[4]&2) { fail(m,MSC_SENSE_ILLEGAL_REQUEST,ASC_MEDIUM_REMOVAL); return; }
        return;
    case SCSI_PREVENT_ALLOW:
        if (!medium_present(m)) { fail(m,MSC_SENSE_NOT_READY,ASC_MEDIUM_NOT_PRESENT); return; }
        m->prevented=(cdb[4]&1) ? 1 : 0;
        return;
    case SCSI_READ10: {
        u32 lba=be32(cdb+2);
        unsigned blocks=be16(cdb+7);
        if (cdb[1]&0xf0) { fail(m,MSC_SENSE_ILLEGAL_REQUEST,ASC_INVALID_FIELD); return; }
        if (!set_medium_phase(m,lba,blocks,0)) { return; }
        begin_data(m,1);
        return;
    }
    case SCSI_WRITE10: {
        u32 lba=be32(cdb+2);
        unsigned blocks=be16(cdb+7);
        if (cdb[1]&0xf0) { fail(m,MSC_SENSE_ILLEGAL_REQUEST,ASC_INVALID_FIELD); return; }
        if (!set_medium_phase(m,lba,blocks,1)) { return; }
        begin_data(m,0);
        return;
    }
    case SCSI_SYNCHRONIZE10:
        if (!medium_present(m)) { fail(m,MSC_SENSE_NOT_READY,ASC_MEDIUM_NOT_PRESENT); return; }
        if (m->medium.sync && !m->medium.sync(m->medium.context)) {
            fail(m,MSC_SENSE_MEDIUM_ERROR,ASC_WRITE_ERROR);
        }
        return;
    default:
        /* FORMAT UNIT lands here too. Refusing is the point: this device never
         * formats, erases or reinitialises media. */
        fail(m,MSC_SENSE_ILLEGAL_REQUEST,ASC_INVALID_COMMAND);
        return;
    }
}
int msc_cbw(Msc *m, const u8 *frame, unsigned size)
{
    unsigned data_len, cdb_len, i;
    if (m->fault) { return -1; }
    if (m->phase==MSC_PHASE_DATA || m->phase==MSC_PHASE_CSW) {
        /* A new CBW during a transfer aborts it. The host is expected to reset
         * the pipe; refusing here keeps the two sides from interleaving. */
        stall_all(m);
        return -1;
    }
    if (size!=MSC_CBW_SIZE || le32(frame)!=MSC_CBW_SIGNATURE) {
        stall_all(m);
        return -1;
    }
    cdb_len=frame[14];
    if (!cdb_len || cdb_len>16) {
        stall_all(m);
        return -1;
    }
    m->tag=le32(frame+4);
    data_len=le32(frame+8);
    m->cdb_len=cdb_len;
    for (i=0; i<16; i++) { m->cdb[i]=i<cdb_len ? frame[15+i] : 0; }
    m->commands++;
    scsi_command(m,m->cdb);
    if (m->status!=MSC_PASSED) {
        /* The host expected data_len bytes and will receive none of them. */
        m->data_total=0;
        m->expect=data_len;
    }
    /* The host may ask for less than the command produces. Transfer what it
     * asked for, report the rest as residue, and do not guess. */
    if (m->data_total>data_len) { m->data_total=data_len; }
    if (m->direction==0 && m->data_total!=m->expect) {
        /* A truncated WRITE would drop blocks the host believes it wrote. */
        m->status=MSC_PHASE_ERROR;
        m->data_total=0;
    }
    if (m->data_total) {
        m->transferred=0;
        m->sector_offset=0;
        m->sector_loaded=0;
        /* Commands that answer from the inline buffer set their lengths but
         * not the phase; entering it here is what makes them transfer. */
        m->phase=MSC_PHASE_DATA;
        return 0;
    }
    prepare_csw(m);
    return 1;
}
static int poll_data_in(Msc *m)
{
    unsigned chunk, left, room;
    int n;
    if (m->inline_data) {
        left=m->inline_len-m->inline_sent;
        if (!left) { prepare_csw(m); return 0; }
        chunk=left<m->link.max_packet ? left : m->link.max_packet;
        n=m->link.send(m->link.context,m->inline_buf+m->inline_sent,chunk);
        if (n<0) { return -1; }
        if (n>0) { m->inline_sent+=chunk; m->transferred+=chunk; }
        return 0;
    }
    left=m->data_total-m->transferred;
    if (!left) { prepare_csw(m); return 0; }
    if (!m->sector_loaded) {
        if (!m->medium.read || !m->medium.read(m->medium.context,m->lba,m->sector)) {
            /* A medium error is a failed command, not a protocol violation:
             * the host expects a CSW with the residue so it can ask REQUEST
             * SENSE. Stalling the pipe the CSW travels on would hide it. */
            sense(m,MSC_SENSE_MEDIUM_ERROR,ASC_READ_ERROR);
            m->status=MSC_FAILED;
            prepare_csw(m);
            return 0;
        }
        m->sector_loaded=1;
        m->sector_offset=0;
    }
    room=MSC_BLOCK-m->sector_offset;
    if (room>left) { room=left; }
    chunk=room<m->link.max_packet ? room : m->link.max_packet;
    n=m->link.send(m->link.context,m->sector+m->sector_offset,chunk);
    if (n<0) { return -1; }
    if (n>0) {
        m->sector_offset+=chunk;
        m->transferred+=chunk;
        if (m->sector_offset==MSC_BLOCK) { m->sector_loaded=0; m->lba++; }
    }
    return 0;
}
static int poll_data_out(Msc *m)
{
    unsigned chunk, left, room;
    int n;
    left=m->data_total-m->transferred;
    if (!left) { prepare_csw(m); return 0; }
    if (!m->sector_loaded) {
        unsigned i;
        for (i=0; i<MSC_BLOCK; i++) { m->sector[i]=0; }
        m->sector_loaded=1;
        m->sector_offset=0;
    }
    room=MSC_BLOCK-m->sector_offset;
    if (room>left) { room=left; }
    chunk=room<m->link.max_packet ? room : m->link.max_packet;
    n=m->link.receive(m->link.context,m->sector+m->sector_offset,chunk);
    if (n<0) { return -1; }
    if (n>0) {
        m->sector_offset+=(unsigned)n;
        m->transferred+=(unsigned)n;
        if (m->sector_offset==MSC_BLOCK) {
            if (!m->medium.write || !m->medium.write(m->medium.context,m->lba,m->sector)) {
                sense(m,MSC_SENSE_MEDIUM_ERROR,ASC_WRITE_ERROR);
                m->status=MSC_FAILED;
                prepare_csw(m);
                return 0;
            }
            m->sector_loaded=0;
            m->lba++;
        }
    }
    return 0;
}
static int poll_csw(Msc *m)
{
    unsigned left=MSC_CSW_SIZE-m->csw_offset, chunk;
    int n;
    if (!left) { m->phase=MSC_PHASE_IDLE; return 1; }
    chunk=left<m->link.max_packet ? left : m->link.max_packet;
    n=m->link.send(m->link.context,m->csw+m->csw_offset,chunk);
    if (n<0) { return -1; }
    if (n>0) { m->csw_offset+=chunk; }
    if (m->csw_offset>=MSC_CSW_SIZE) {
        m->phase=MSC_PHASE_IDLE;
        if (m->status!=MSC_PASSED) { m->errors++; }
        return 1;
    }
    if (n==0) { return 0; }
    return 0;
}
int msc_poll(Msc *m)
{
    if (m->fault) { return -1; }
    switch (m->phase) {
    case MSC_PHASE_DATA:
        return m->direction ? poll_data_in(m) : poll_data_out(m);
    case MSC_PHASE_CSW:
        return poll_csw(m);
    case MSC_PHASE_IDLE:
        return 1;
    default:
        return 0;   /* stalled: waiting for a Bulk-Only Mass Storage Reset */
    }
}
void msc_reset(Msc *m)
{
    m->phase=MSC_PHASE_IDLE;
    m->expect=m->data_total=m->transferred=0;
    m->sector_offset=0;
    m->sector_loaded=0;
    m->inline_data=0;
    m->inline_len=m->inline_sent=0;
    m->csw_offset=0;
    m->status=MSC_PASSED;
    /* The next real command must report that a reset happened. */
    m->unit_attention=1;
    m->sense_key=MSC_SENSE_NONE;
    m->sense_asc=m->sense_ascq=0;
    m->resets++;
    if (m->link.reset) { m->link.reset(m->link.context); }
}
void msc_init(Msc *m, const MscMedium *medium, const MscLink *link)
{
    *m=(Msc){.medium=*medium,.link=*link};
    /* A link with no way to move bytes is a programming error, and calling
     * through it would fault the target. Refuse instead. */
    m->fault=(!link->send || !link->receive || !link->max_packet);
    msc_reset(m);
    m->resets=0;
    m->unit_attention=0;
    m->fault=m->fault;
    m->commands=0;
    m->errors=0;
    m->stalls=0;
}
int msc_idle(const Msc *m) { return m->phase==MSC_PHASE_IDLE; }
