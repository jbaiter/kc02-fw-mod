/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "msc_peer.h"
static void put32(u8 *p, u32 v)
{
    p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24);
}
static int in_range(const Peer *p, u32 lba)
{
    return (lba+1)<=p->blocks && (lba+1)<=(u32)(PEER_DATA_MAX/MSC_BLOCK);
}
static int medium_read(void *ctx, u32 lba, u8 *out)
{
    Peer *p=ctx;
    p->reads++;
    if (p->fail_read || !in_range(p,lba)) { return 0; }
    memcpy(out,p->medium+lba*MSC_BLOCK,MSC_BLOCK);
    return 1;
}
static int medium_write(void *ctx, u32 lba, const u8 *in)
{
    Peer *p=ctx;
    p->writes++;
    if (p->fail_write || !in_range(p,lba)) { return 0; }
    memcpy(p->medium+lba*MSC_BLOCK,in,MSC_BLOCK);
    return 1;
}
static int medium_sync(void *ctx) { ((Peer *)ctx)->syncs++; return 1; }
static int link_send(void *ctx, const u8 *data, unsigned size)
{
    Peer *p=ctx;
    if (p->stalled_in) { return -1; }
    if (size>PEER_DATA_MAX-p->out_size) { return -1; }
    memcpy(p->out+p->out_size,data,size);
    p->out_size+=size;
    return 1;
}
static int link_receive(void *ctx, u8 *out, unsigned size)
{
    Peer *p=ctx;
    unsigned left=p->in_size-p->in_at, take;
    if (p->stalled_out) { return -1; }
    if (!left) { return 0; }
    take=left<size ? left : size;
    memcpy(out,p->in+p->in_at,take);
    p->in_at+=take;
    return (int)take;
}
static void link_stall(void *ctx, int in)
{
    Peer *p=ctx;
    if (in) { p->stalls_in++; p->stalled_in=1; }
    else { p->stalls_out++; p->stalled_out=1; }
}
static void link_reset(void *ctx)
{
    Peer *p=ctx;
    p->resets++;
    p->stalled_in=p->stalled_out=0;
}
void peer_init(Peer *p, u32 blocks, int writable)
{
    MscMedium medium={0,0,0,medium_read,medium_write,medium_sync};
    MscLink link={0,64,link_send,link_receive,link_stall,link_reset};
    u32 i;
    unsigned j;
    memset(p,0,sizeof(*p));
    p->blocks=blocks;
    p->writable=writable;
    for (i=0; i<blocks && (i+1)<=(u32)(PEER_DATA_MAX/MSC_BLOCK); i++) {
        for (j=0; j<MSC_BLOCK; j++) { p->medium[i*MSC_BLOCK+j]=(u8)(i*7+j); }
    }
    medium.context=p;
    medium.blocks=blocks;
    medium.writable=writable;
    link.context=p;
    msc_init(&p->msc,&medium,&link);
    p->resets=0;
}
int peer_command(Peer *p, const u8 *cdb, unsigned cdb_len, unsigned data_len,
                 const u8 *data_in, unsigned data_in_size)
{
    u8 cbw[MSC_CBW_SIZE];
    unsigned guard;
    p->out_size=0;
    p->in_size=p->in_at=0;
    p->csw_size=0;
    if (data_in_size>PEER_DATA_MAX) { return -1; }
    if (data_in && data_in_size) { memcpy(p->in,data_in,data_in_size); p->in_size=data_in_size; }
    memset(cbw,0,sizeof(cbw));
    put32(cbw,MSC_CBW_SIGNATURE);
    put32(cbw+4,++p->tag);
    put32(cbw+8,data_len);
    cbw[14]=(u8)cdb_len;
    memcpy(cbw+15,cdb,cdb_len>16 ? 16 : cdb_len);
    if (msc_cbw(&p->msc,cbw,MSC_CBW_SIZE)<0) { return -1; }
    /* Bounded: a broken engine must not spin the test forever. */
    for (guard=0; guard<4096 && !msc_idle(&p->msc); guard++) {
        if (msc_poll(&p->msc)<0) { break; }
    }
    if (p->out_size>=MSC_CSW_SIZE) {
        memcpy(p->csw,p->out+p->out_size-MSC_CSW_SIZE,MSC_CSW_SIZE);
        p->csw_size=MSC_CSW_SIZE;
        p->out_size-=MSC_CSW_SIZE;
    }
    return p->csw_size==MSC_CSW_SIZE ? p->csw[12] : -1;
}
const u8 *peer_out(Peer *p) { return p->out; }
unsigned peer_out_size(Peer *p) { return p->out_size; }
const u8 *peer_medium(Peer *p) { return p->medium; }
const u8 *peer_csw(Peer *p) { return p->csw; }
u32 peer_csw32(Peer *p, unsigned offset)
{
    return (u32)p->csw[offset]|((u32)p->csw[offset+1]<<8)|
           ((u32)p->csw[offset+2]<<16)|((u32)p->csw[offset+3]<<24);
}
unsigned peer_residue(Peer *p) { return peer_csw32(p,8); }
unsigned peer_reads(Peer *p) { return p->reads; }
unsigned peer_writes(Peer *p) { return p->writes; }
unsigned peer_syncs(Peer *p) { return p->syncs; }
unsigned peer_stalls(Peer *p, int direction)
{
    return direction==PEER_STALL_IN ? p->stalls_in : p->stalls_out;
}
unsigned peer_resets(Peer *p) { return p->resets; }
u32 peer_tag(Peer *p) { return p->tag; }
void peer_fail_read(Peer *p, int fail) { p->fail_read=fail; }
void peer_fail_write(Peer *p, int fail) { p->fail_write=fail; }
Msc *peer_msc(Peer *p) { return &p->msc; }
int peer_raw_cbw(Peer *p, const u8 *frame, unsigned size)
{
    return msc_cbw(&p->msc,frame,size);
}
void peer_raw_reset(Peer *p) { msc_reset(&p->msc); }
