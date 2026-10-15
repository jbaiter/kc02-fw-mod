/* SPDX-License-Identifier: MIT */
#include "webusb.h"
/* Little-endian byte order of {3408b638-09a9-47a0-8bfd-a0768815b665}, which is
 * what the specification requires and what Chrome compares against. Getting
 * this backwards makes the capability invisible, with no error anywhere. */
static const u8 webusb_uuid[16]={
    0x38,0xb6,0x08,0x34,0xa9,0x09,0xa0,0x47,
    0x8b,0xfd,0xa0,0x76,0x88,0x15,0xb6,0x65
};
static const char landing[]=WEBUSB_URL;
static unsigned text_length(const char *s) { unsigned n=0; while (s[n]) { n++; } return n; }
static u16 le16(const u8 *p) { return (u16)(p[0]|((u16)p[1]<<8)); }
static u32 le32(const u8 *p)
{
    return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);
}
static void put16(u8 *p, unsigned v) { p[0]=(u8)v; p[1]=(u8)(v>>8); }
static void put32(u8 *p, u32 v) { unsigned i; for (i=0; i<4; i++) { p[i]=(u8)(v>>(i*8)); } }
static u32 crc_step(u32 crc, const u8 *bytes, unsigned size)
{
    unsigned i, b;
    for (i=0; i<size; i++) {
        crc^=bytes[i];
        for (b=0; b<8; b++) { crc=(crc>>1)^(0xedb88320u&(0u-(crc&1))); }
    }
    return crc;
}
/* Reads one staged file by name, translated to a range of the container. */
typedef struct { WebUsb *webusb; u32 window; } Window;
static int window_read(void *context, u32 offset, u8 *out, unsigned size)
{
    Window *win=context;
    return file_reader_read(&win->webusb->stage,win->window+offset,out,size);
}
static int entry_index(WebUsb *w, const char *name)
{
    unsigned i;
    for (i=0; i<w->status.files; i++) {
        if (!w->dotted[i][0]) { continue; }
        {
            unsigned j;
            for (j=0; ; j++) {
                if (w->dotted[i][j]!=name[j]) { break; }
                if (!name[j]) { return (int)i; }
            }
        }
    }
    return -1;
}
static int bundle_read(void *context, const char *name, u32 offset, u8 *out, unsigned size)
{
    WebUsb *w=context;
    int index=entry_index(w,name);
    if (index<0) { return 0; }
    return file_reader_read(&w->stage,w->data_offset+w->entry_offset[index]+offset,out,size);
}
/* The theme writer is the bounded card writer: open, stream, commit.
 *
 * The handle is the WebUsb itself, because the engine keeps exactly one pending
 * writer and the flag guarding it has to be cleared on every exit. Handing out
 * the FatWrite alone loses the way back to that flag, which leaves the next file
 * unable to start. */
static int bundle_begin(void *context, const char *name, u32 size, void **handle)
{
    WebUsb *w=context;
    if (w->pending_open) { return 0; }
    if (!fat_write_begin(w->config.volume,name,size,&w->pending)) { return 0; }
    w->pending_open=1;
    *handle=w;
    return 1;
}
static int bundle_data(void *handle, const u8 *bytes, unsigned size)
{
    WebUsb *w=handle;
    return fat_write_data(&w->pending,bytes,size);
}
static int bundle_end(void *handle)
{
    WebUsb *w=handle;
    int ok=fat_write_end(&w->pending);
    w->pending_open=0;
    return ok;
}
static void bundle_abort(void *handle)
{
    WebUsb *w=handle;
    fat_write_abort(&w->pending);
    w->pending_open=0;
}
/* A zero instance rather than a compound literal: naming no fields at all
 * trips -Wmissing-field-initializers on this toolchain, and this object is
 * large enough that a partial reset would be easy to get wrong. */
static const WebUsb blank;
void webusb_init(WebUsb *w, const WebUsbConfig *config)
{
    *w=blank;
    w->config=*config;
    webusb_status_sync(w);
}
void webusb_status_sync(WebUsb *w)
{
    u8 *b=w->status_bytes;
    put32(b+0,0x55534257u);   /* "WBU\0" */
    put32(b+4,w->status.state);
    put32(b+8,w->status.result);
    put32(b+12,w->status.kind);
    put32(b+16,w->status.received);
    put32(b+20,w->status.total);
    put32(b+24,w->status.version);
    put32(b+28,w->status.files);
}
int webusb_status_read(WebUsb *w, u8 *out, unsigned size)
{
    unsigned n;
    if (!w || !out || !size) { return 0; }
    webusb_status_sync(w);
    n=size<WEBUSB_STATUS_BYTES ? size : WEBUSB_STATUS_BYTES;
    for (unsigned i=0; i<n; i++) { out[i]=w->status_bytes[i]; }
    return (int)n;
}
/* Bounds the container before a byte of it reaches the card. */
static int check_header(WebUsb *w)
{
    const u8 *h=w->header;
    unsigned kind, count, i;
    u32 limit;
    /* Eight bytes: "LUMAUPD" and a zero, so the magic cannot be a prefix of
     * something longer. */
    for (i=0; i<7; i++) { if (h[i]!=(u8)WEBUSB_MAGIC[i]) { return 0; } }
    if (h[7]!=0) { return 0; }
    if (le16(h+8)!=WEBUSB_FORMAT || le16(h+10)!=WEBUSB_HEADER_BYTES) { return 0; }
    kind=le16(h+12); count=le16(h+14);
    if (!count || count>WEBUSB_FILES_MAX) { return 0; }
    if (kind!=WEBUSB_FIRMWARE && kind!=WEBUSB_THEME) { return 0; }
    if (kind==WEBUSB_FIRMWARE && count!=1) { return 0; }
    limit = kind==WEBUSB_FIRMWARE ? SLOT_SIZE+IMAGE_HEADER : THEME_BUNDLE_BYTES;
    w->status.kind=kind;
    w->status.files=count;
    w->status.total=le32(h+16);
    w->status.received=0;
    w->crc_expected=le32(h+20);
    w->crc_actual=~0u;
    /* The byte count the data phase will accept. Leaving this unset makes every
     * upload look like it ended early and fail its CRC. */
    w->remaining=w->status.total;
    if (!w->status.total || w->status.total>limit) { return 0; }
    w->header_need=WEBUSB_HEADER_BYTES+WEBUSB_ENTRY_BYTES*count;
    if (w->header_need>sizeof(w->header)) { return 0; }
    return 1;
}
static int check_entries(WebUsb *w)
{
    const u8 *h=w->header+WEBUSB_HEADER_BYTES;
    unsigned count=w->status.files, i;
    u32 sum=0, offset=0;
    if (w->status.kind==WEBUSB_THEME) { w->bundle=(ThemeBundle){.count=0,.bytes=0}; }
    for (i=0; i<count; i++) {
        const u8 *e=h+i*WEBUSB_ENTRY_BYTES;
        char padded[13];
        unsigned j;
        u32 length=le32(e+12);
        for (j=0; j<11; j++) { padded[j]=(char)e[j]; }
        padded[11]=0;
        fat_name_dotted(padded,w->dotted[i]);
        if (!w->dotted[i][0] || !length) { return 0; }
        w->entry_offset[i]=offset;
        w->entry_length[i]=length;
        offset+=length;
        sum+=length;
        if (w->status.kind==WEBUSB_FIRMWARE) {
            /* The single artifact is the firmware container itself. */
            if (length<IMAGE_HEADER || length>SLOT_SIZE+IMAGE_HEADER) { return 0; }
        } else if (!theme_bundle_add(&w->bundle,w->dotted[i],length,le32(e+16))) {
            return 0;
        }
    }
    if (sum!=w->status.total) { return 0; }
    if (w->status.kind==WEBUSB_THEME && w->bundle.bytes!=w->status.total) { return 0; }
    w->data_offset=w->header_need;
    return 1;
}
int webusb_feed(WebUsb *w, const u8 *bytes, unsigned size)
{
    unsigned i=0, take;
    if (!w || !bytes || !size) { return 1; }
    if (w->status.state!=WEBUSB_STAGING) { return 0; }
    while (i<size) {
        if (w->header_have<w->header_need) {
            take=w->header_need-w->header_have;
            if (take>size-i) { take=size-i; }
            for (unsigned j=0; j<take; j++) { w->header[w->header_have+j]=bytes[i+j]; }
            w->header_have+=take;
            i+=take;
            if (w->header_have==WEBUSB_HEADER_BYTES && !w->crc_expected) {
                if (!check_header(w)) { return 0; }
            }
            if (w->header_have==w->header_need && !w->writing) {
                if (!check_entries(w)) { return 0; }
                if (!w->config.volume || !w->config.volume->mounted ||
                    w->config.volume->owner!=SD_LOCAL) { return 0; }
                if (!fat_write_begin(w->config.volume,WEBUSB_STAGE_NAME,
                                     w->header_need+w->status.total,&w->writer)) {return 0;
                    return 0;
                }
                w->writing=1;
                if (!fat_write_data(&w->writer,w->header,w->header_need)) { return 0; }
            }
            continue;
        }
        if (!w->writing || !w->remaining) { break; }
        take=size-i;
        if (take>w->remaining) { take=w->remaining; }
        if (!fat_write_data(&w->writer,bytes+i,take)) { return 0; }
        /* The CRC is over exactly the bytes that reach the card. */
        w->crc_actual=crc_step(w->crc_actual,bytes+i,take);
        w->remaining-=take;
        w->status.received+=take;
        i+=take;
    }
    if (w->writing && w->remaining==0) {
        if ((w->crc_actual^0xffffffffu)!=w->crc_expected) {
            fat_write_abort(&w->writer);
            w->writing=0;
            w->status.state=WEBUSB_FAILED;
            w->status.result=WEBUSB_RESULT_BAD_CRC;
            return 0;
        }
        if (!fat_write_end(&w->writer)) {
            w->writing=0;
            w->status.state=WEBUSB_FAILED;
            w->status.result=WEBUSB_RESULT_MEDIUM;
            return 0;
        }
        w->writing=0;
        w->status.state=WEBUSB_STAGED;
        w->status.result=0;
    }
    return 1;
}
static void apply(WebUsb *w)
{
    if (w->status.kind==WEBUSB_FIRMWARE) {
        Window window={w,w->data_offset};
        ImageSource source={&window,window_read,w->status.total};
        ImageInfo info;
        UpdateResult outcome;
        if (!file_reader_open(&w->stage,w->config.volume,WEBUSB_STAGE_NAME)) {
            w->status.state=WEBUSB_FAILED;
            w->status.result=WEBUSB_RESULT_MEDIUM;
            return;
        }
        if (!image_validate(&source,w->config.board_bit,&info)) {
            w->status.state=WEBUSB_FAILED;
            w->status.result=WEBUSB_RESULT_BAD_IMAGE;
            return;
        }
        w->status.version=info.version;
        if (!w->config.nor) {
            /* Validated, and there is nothing here that can write flash. */
            w->status.state=WEBUSB_FAILED;
            w->status.result=WEBUSB_RESULT_NO_WRITER;
            return;
        }
        if (!w->config.locally_confirmed || !w->config.recovery_verified) {
            w->status.state=WEBUSB_DENIED;
            w->status.result=WEBUSB_RESULT_NOT_CONFIRMED;
            return;
        }
        outcome=update_apply(w->config.nor,&source,w->config.board_bit,
                             w->config.locally_confirmed,w->config.recovery_verified);
        if (outcome!=UPDATE_OK) {
            w->status.state=outcome==UPDATE_DENIED ? WEBUSB_DENIED : WEBUSB_FAILED;
            w->status.result=WEBUSB_RESULT_ENGINE_BASE+(unsigned)outcome;
            return;
        }
        w->status.state=WEBUSB_APPLIED;
        w->status.result=0;
        return;
    }
    {
        BundleSource source={w,bundle_read};
        BundleWriter writer={w,bundle_begin,bundle_data,bundle_end,bundle_abort};
        ThemeBundleResult checked;
        if (!file_reader_open(&w->stage,w->config.volume,WEBUSB_STAGE_NAME)) {
            w->status.state=WEBUSB_FAILED;
            w->status.result=WEBUSB_RESULT_MEDIUM;
            return;
        }
        checked=theme_bundle_check(&w->bundle,&source,0);
        if (checked!=THEME_BUNDLE_OK) {
            w->status.state=WEBUSB_FAILED;
            w->status.result=WEBUSB_RESULT_THEME_BASE+(unsigned)checked;
            return;
        }
        /* The reader rewinds by reopening, so both passes stream bounded. */
        checked=theme_bundle_commit(&w->bundle,&source,&writer);
        if (checked!=THEME_BUNDLE_OK) {
            w->status.state=WEBUSB_FAILED;
            w->status.result=WEBUSB_RESULT_THEME_BASE+(unsigned)checked;
            return;
        }
        w->status.state=WEBUSB_APPLIED;
        w->status.result=0;
    }
}
void webusb_command(WebUsb *w, unsigned command)
{
    if (!w) { return; }
    switch (command) {
    case WEBUSB_CMD_BEGIN:
        /* A new upload discards whatever was staged before it. */
        if (w->writing) { fat_write_abort(&w->writer); w->writing=0; }
        if (w->stage_open) { file_reader_close(&w->stage); w->stage_open=0; }
        if (w->pending_open) { fat_write_abort(&w->pending); w->pending_open=0; }
        w->header_have=0;
        w->header_need=WEBUSB_HEADER_BYTES;
        w->remaining=0;
        w->crc_expected=0;
        w->crc_actual=~0u;
        w->bundle=(ThemeBundle){.count=0,.bytes=0};
        w->status=(WebUsbStatus){.state=WEBUSB_STAGING};
        w->begins++;
        return;
    case WEBUSB_CMD_COMMIT:
        w->commits++;
        if (w->status.state!=WEBUSB_STAGED) {
            w->status.state=WEBUSB_DENIED;
            w->status.result=WEBUSB_RESULT_NOT_STAGED;
            return;
        }
        apply(w);
        return;
    case WEBUSB_CMD_ABORT:
        w->aborts++;
        if (w->writing) { fat_write_abort(&w->writer); w->writing=0; }
        if (w->stage_open) { file_reader_close(&w->stage); w->stage_open=0; }
        if (w->pending_open) { fat_write_abort(&w->pending); w->pending_open=0; }
        w->status=(WebUsbStatus){.state=WEBUSB_IDLE};
        return;
    default:
        return;
    }
}
unsigned webusb_bos(u8 *out, unsigned cap)
{
    unsigned i;
    if (!out || cap<29) { return 0; }
    out[0]=5; out[1]=0x0f; put16(out+2,29); out[4]=1;
    out[5]=24; out[6]=0x10; out[7]=0x05; out[8]=0;
    for (i=0; i<16; i++) { out[9+i]=webusb_uuid[i]; }
    put16(out+25,0x0100);                  /* bcdVersion */
    out[27]=WEBUSB_VENDOR_CODE;
    out[28]=landing[0] ? 1 : 0;            /* iLandingPage 0 means "none" */
    return 29;
}
unsigned webusb_url(u8 *out, unsigned cap)
{
    unsigned scheme, offset, n, i;
    if (!out || !landing[0]) { return 0; }
    /* Only https is offered: a plaintext landing page would let anyone on the
     * path change the page that talks to the device. */
    if (landing[0]=='h' && landing[1]=='t' && landing[2]=='t' && landing[3]=='p' &&
        landing[4]=='s' && landing[5]==':' && landing[6]=='/' && landing[7]=='/') {
        scheme=WEBUSB_URL_SCHEME_HTTPS;
        offset=8;
    } else {
        return 0;
    }
    n=text_length(landing)-offset;
    if (!n || 3+n>cap) { return 0; }
    out[0]=(u8)(3+n); out[1]=WEBUSB_DESCRIPTOR_TYPE_URL; out[2]=(u8)scheme;
    for (i=0; i<n; i++) { out[3+i]=(u8)landing[offset+i]; }
    return 3+n;
}
