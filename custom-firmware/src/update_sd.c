/* SPDX-License-Identifier: MIT */
#include "update_sd.h"
/* Discovery and validation read the file twice: the engine validates a source
 * and then copies it. FileReader reopens on rewind, so the file never has to be
 * held in RAM and the reader stays bounded. */
typedef struct {
    FileReader file;
    ImageSource source;
} SdImage;
static int image_read(void *context, u32 offset, u8 *out, unsigned size)
{
    return file_reader_read((FileReader *)context, offset, out, size);
}
static int image_open(SdImage *img, Fat *volume, const char *name)
{
    if (!file_reader_open(&img->file, volume, name)) { return 0; }
    img->source.context = &img->file;
    img->source.read = image_read;
    img->source.size = img->file.size;
    return 1;
}
static UpdateSdResult prepare(Fat *volume, SdImage *img, ImageSource *source, UpdateSdReport *local)
{
    if (!volume || !img || !source || !local) { return UPDATE_SD_NO_CARD; }
    if (!volume->mounted || !volume->read) { return UPDATE_SD_NO_CARD; }
    /* One writer at a time. A mounted-for-host card is not ours to read. */
    if (volume->owner!=SD_LOCAL) { return UPDATE_SD_BUSY; }
    if (!image_open(img,volume,UPDATE_SD_NAME)) { return UPDATE_SD_NO_FILE; }
    *source=img->source;
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
    local.file_bytes=img.file.size;
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
    local.file_bytes=img.file.size;
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
