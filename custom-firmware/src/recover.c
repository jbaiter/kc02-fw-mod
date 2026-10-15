/* SPDX-License-Identifier: MIT */
#include "recover.h"
#define CANDIDATE_NAME "DESTBIN.BIN"
static FileStream stream;
static int stream_read(void *context, u32 offset, u8 *out, unsigned size)
{
    FileStream *s=context;
    /* The validator reads forward only. Refuse anything else instead of
     * silently mis-serving a seek it cannot honour. */
    if (offset!=s->done) { return 0; }
    return fat_stream_read(s,out,size)==(int)size;
}
RecoverResult recover_check(Fat *volume, SectorRead read, u32 capacity, RecoverReport *report)
{
    ImageSource source;
    RecoverReport local={RECOVER_NO_CARD,{0,0,0,0},capacity};
    if (report) { *report=local; }
    if (!volume || !read || !capacity) { return RECOVER_NO_CARD; }
    /* Read-only mount: a null write callback makes fat_create refuse. */
    if (!fat_mount(volume,read,0,capacity)) {
        local.result=RECOVER_NO_FILESYSTEM;
        if (report) { *report=local; }
        return RECOVER_NO_FILESYSTEM;
    }
    if (!fat_stream_open(volume,CANDIDATE_NAME,&stream)) {
        local.result=RECOVER_NO_CANDIDATE;
        if (report) { *report=local; }
        return RECOVER_NO_CANDIDATE;
    }
    source=(ImageSource){&stream,stream_read,stream.size};
    {
        DestBinResult checked=destbin_inspect(&source,&local.info);
        if (checked!=DESTBIN_OK) {
            local.result=(checked==DESTBIN_IO) ? RECOVER_CANDIDATE_IO : RECOVER_CANDIDATE_INVALID;
            if (report) { *report=local; }
            return local.result;
        }
    }
    /* Structure and CRC passed. No trusted snapshot of the running boot bytes
     * is available yet, so the protected-prefix comparison cannot run. Report
     * that rather than claiming the candidate is verified. */
    local.result=RECOVER_PREFIX_UNAVAILABLE;
    if (report) { *report=local; }
    return local.result;
}
