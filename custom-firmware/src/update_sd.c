/* SPDX-License-Identifier: MIT */
#include "update_sd.h"
typedef struct {
    Fat *volume;
    char name[13];
    FileStream stream;
    u32 stream_at;      /* Offset the stream will deliver next. */
    u32 size;
    int stream_open;
} SdImage;
static unsigned name_length(const char *s) { unsigned n=0; while (s[n]) { n++; } return n; }
/* The FAT lookup is written for short uppercase names; a user typing
 * /firmware.img means the same file, so the spelling is normalised here. */
static int normalise_name(const char *in, char out[13])
{
    unsigned i, n=name_length(in);
    if (!n || n>12) { return 0; }
    for (i=0; i<n; i++) {
        char c=in[i];
        if (c>='a' && c<='z') { c=(char)(c-'a'+'A'); }
        out[i]=c;
    }
    out[n]=0;
    return 1;
}
static void image_close(SdImage *img)
{
    img->stream_open=0;
    img->stream_at=0;
}
static int image_open(SdImage *img)
{
    if (!fat_stream_open(img->volume,img->name,&img->stream)) { return 0; }
    img->size=img->stream.size;
    img->stream_at=0;
    img->stream_open=1;
    return 1;
}
/* Bounded: a rewind is one reopen, and a forward skip reads and discards in
 * 256-byte steps, so no offset can make this allocate or loop unbounded. */
static int image_read(void *context, u32 offset, u8 *out, unsigned size)
{
    SdImage *img=context;
    u8 discard[256];
    if (!img || !img->stream_open) { return 0; }
    if (offset<img->stream_at) {
        image_close(img);
        if (!image_open(img)) { return 0; }
    }
    while (img->stream_at<offset) {
        unsigned want=offset-img->stream_at;
        int got;
        if (want>sizeof(discard)) { want=sizeof(discard); }
        got=fat_stream_read(&img->stream,discard,want);
        if (got!=(int)want) { return 0; }
        img->stream_at+=want;
    }
    if (fat_stream_read(&img->stream,out,size)!=(int)size) { return 0; }
    img->stream_at+=size;
    return 1;
}
static UpdateSdResult prepare(Fat *volume, SdImage *img, ImageSource *source, UpdateSdReport *local)
{
    if (!volume || !img || !source || !local) { return UPDATE_SD_NO_CARD; }
    if (!volume->mounted || !volume->read) { return UPDATE_SD_NO_CARD; }
    /* One writer at a time. A mounted-for-host card is not ours to read. */
    if (volume->owner!=SD_LOCAL) { return UPDATE_SD_BUSY; }
    if (!normalise_name(UPDATE_SD_NAME,img->name)) { return UPDATE_SD_NO_FILE; }
    img->volume=volume;
    if (!image_open(img)) { return UPDATE_SD_NO_FILE; }
    source->context=img;
    source->read=image_read;
    source->size=img->size;
    return UPDATE_SD_OK;
}
UpdateSdResult update_sd_check(Fat *volume, u32 board_bit, UpdateSdReport *report)
{
    SdImage img;
    ImageSource source;
    UpdateSdReport local={.result=UPDATE_SD_NO_CARD};
    UpdateSdResult status;
    ImageInfo remote;
    if (report) { *report=local; }
    status=prepare(volume,&img,&source,&local);
    if (status!=UPDATE_SD_OK) {
        if (report) { *report=local; }
        return status;
    }
    local.file_bytes=img.size;
    if (!image_validate(&source,board_bit,&remote)) {
        local.result=UPDATE_SD_BAD_FILE;
        if (report) { *report=local; }
        return local.result;
    }
    local.info=remote;
    local.validated=1;
    local.result=UPDATE_SD_READY;
    if (report) { *report=local; }
    return local.result;
}
UpdateSdResult update_sd_apply(Fat *volume, const Nor *nor, u32 board_bit,
                               int physical_confirmation, int recovery_verified,
                               UpdateSdReport *report)
{
    SdImage img;
    ImageSource source;
    UpdateSdReport local={.result=UPDATE_SD_NO_CARD};
    UpdateSdResult status;
    ImageInfo remote;
    UpdateResult outcome;
    if (report) { *report=local; }
    status=prepare(volume,&img,&source,&local);
    if (status!=UPDATE_SD_OK) {
        if (report) { *report=local; }
        return status;
    }
    local.file_bytes=img.size;
    /* Validate the whole file before the engine is allowed to touch anything,
     * so a bad image is reported as a bad image and not as an I/O failure
     * halfway through a write. */
    if (!image_validate(&source,board_bit,&remote)) {
        local.result=UPDATE_SD_BAD_FILE;
        if (report) { *report=local; }
        return local.result;
    }
    local.info=remote;
    local.validated=1;
    if (!nor || !nor->read || !nor->erase || !nor->program) {
        /* Honest: the file is good, and nothing here can write it. */
        local.result=UPDATE_SD_NO_WRITER;
        if (report) { *report=local; }
        return local.result;
    }
    if (!physical_confirmation || !recovery_verified) {
        local.result=UPDATE_SD_DENIED;
        if (report) { *report=local; }
        return local.result;
    }
    /* The engine re-reads the source from the beginning; the reader reopens. */
    outcome=update_apply(nor,&source,board_bit,physical_confirmation,recovery_verified);
    switch (outcome) {
    case UPDATE_OK: local.result=UPDATE_SD_OK; break;
    case UPDATE_DENIED: local.result=UPDATE_SD_DENIED; break;
    case UPDATE_INVALID: local.result=UPDATE_SD_BAD_FILE; break;
    case UPDATE_STALE: local.result=UPDATE_SD_STALE; break;
    case UPDATE_NO_BOOT: local.result=UPDATE_SD_NO_BOOT; break;
    default: local.result=UPDATE_SD_IO; break;
    }
    if (report) { *report=local; }
    return local.result;
}
