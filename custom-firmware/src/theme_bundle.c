/* SPDX-License-Identifier: MIT */
#include "theme_bundle.h"
static unsigned text_length(const char *s) { unsigned n=0; while (s[n]) { n++; } return n; }
static int same(const char *a, const char *b)
{
    unsigned i;
    for (i=0; ; i++) {
        if (a[i]!=b[i]) { return 0; }
        if (!a[i]) { return 1; }
    }
}
/* Uppercase 8.3, because that is what the FAT layer looks up and what the
 * config's own image-name checks already require. */
static int normalise(const char *in, char out[13])
{
    unsigned i, n=text_length(in), stem=0, suffix=0, dot=0;
    if (!n || n>12) { return 0; }
    for (i=0; i<13; i++) { out[i]=0; }
    for (i=0; i<n; i++) {
        char c=in[i];
        if (c>='a' && c<='z') { c=(char)(c-'a'+'A'); }
        if (c=='.') {
            if (dot || !stem) { return 0; }
            dot=1;
            continue;
        }
        if (!((c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_')) { return 0; }
        if (dot) {
            if (suffix==3) { return 0; }
            out[8+suffix++]=c;
        } else {
            if (stem==8) { return 0; }
            out[stem++]=c;
        }
    }
    if (!stem) { return 0; }
    for (i=stem; i<8; i++) { out[i]=' '; }
    for (i=8+suffix; i<11; i++) { out[i]=' '; }
    return 1;
}
static int find(const ThemeBundle *bundle, const char *name)
{
    char normal[13];
    unsigned i;
    if (!normalise(name,normal)) { return -1; }
    for (i=0; i<bundle->count; i++) {
        if (same(bundle->entry[i].name,normal)) { return (int)i; }
    }
    return -1;
}
/* Entries are compared in the padded 8.3 space, but both callbacks are given
 * the dotted form the FAT layer and the config both use, so a caller never has
 * to know about the padding. */
static void dotted(const char *padded, char out[13])
{
    unsigned i, stem=0, suffix=0, last=0;
    for (i=0; i<8; i++) { if (padded[i]!=' ') { stem=i+1; } }
    for (i=8; i<11; i++) { if (padded[i]!=' ') { last=i+1; } }
    suffix=last>8 ? last-8 : 0;
    for (i=0; i<stem; i++) { out[i]=padded[i]; }
    if (suffix) {
        out[stem]='.';
        for (i=0; i<suffix; i++) { out[stem+1+i]=padded[8+i]; }
        out[stem+1+suffix]=0;
    } else {
        out[stem]=0;
    }
}
static u32 crc_step(u32 crc, const u8 *bytes, unsigned size)
{
    unsigned i, b;
    for (i=0; i<size; i++) {
        crc^=bytes[i];
        for (b=0; b<8; b++) { crc=(crc>>1)^(0xedb88320u&(0u-(crc&1))); }
    }
    return crc;
}
int theme_bundle_add(ThemeBundle *bundle, const char *name, u32 size, u32 crc)
{
    char normal[13];
    unsigned i;
    if (!bundle || !normalise(name,normal)) { return 0; }
    if (size>THEME_BUNDLE_FILE_MAX) { return 0; }
    for (i=0; i<bundle->count; i++) {
        if (same(bundle->entry[i].name,normal)) {
            /* Replacing keeps the running total honest. */
            bundle->bytes-=bundle->entry[i].size;
            bundle->entry[i].size=size;
            bundle->entry[i].crc=crc;
            bundle->bytes+=size;
            return 1;
        }
    }
    if (bundle->count>=THEME_BUNDLE_MAX) { return 0; }
    bundle->entry[bundle->count].size=size;
    bundle->entry[bundle->count].crc=crc;
    for (i=0; i<13; i++) { bundle->entry[bundle->count].name[i]=normal[i]; }
    bundle->count++;
    bundle->bytes+=size;
    return 1;
}
/* Validates one image from its header and its declared length, then walks the
 * whole file to check the CRC. Bounded: 54 bytes plus a 512-byte window. */
static ThemeBundleResult check_image(const ThemeBundle *bundle, const BundleSource *source,
                                     const char *name)
{
    u8 header[54], body[512];
    char file[13];
    u32 crc=~0u, done;
    int index=find(bundle,name);
    if (index<0) { return THEME_BUNDLE_MISSING_IMAGE; }
    dotted(bundle->entry[index].name,file);
    if (!source->read || !source->read(source->context,file,0,header,sizeof(header))) {
        return THEME_BUNDLE_IO;
    }
    if (!bmp_check(header,bundle->entry[index].size)) { return THEME_BUNDLE_BAD_IMAGE; }
    crc=crc_step(crc,header,sizeof(header));
    for (done=sizeof(header); done<bundle->entry[index].size; ) {
        unsigned want=bundle->entry[index].size-done;
        if (want>sizeof(body)) { want=sizeof(body); }
        if (!source->read(source->context,file,done,body,want)) { return THEME_BUNDLE_IO; }
        crc=crc_step(crc,body,want);
        done+=want;
    }
    if ((crc^0xffffffffu)!=bundle->entry[index].crc) { return THEME_BUNDLE_BAD_IMAGE; }
    return THEME_BUNDLE_OK;
}
ThemeBundleResult theme_bundle_check(const ThemeBundle *bundle, const BundleSource *source,
                                     void *config)
{
    u8 text[THEME_CONFIG_BYTES];
    Theme parsed;
    unsigned i;
    int used[THEME_BUNDLE_MAX];
    int config_index;
    ThemeBundleResult status;
    if (!bundle || !source || !source->read || !bundle->count) {
        return THEME_BUNDLE_MISSING_CONFIG;
    }
    if (bundle->bytes>THEME_BUNDLE_BYTES) { return THEME_BUNDLE_TOO_BIG; }
    config_index=find(bundle,THEME_CONFIG);
    if (config_index<0) { return THEME_BUNDLE_MISSING_CONFIG; }
    for (i=0; i<bundle->count; i++) { used[i]=0; }
    used[config_index]=1;
    if (bundle->entry[config_index].size>THEME_CONFIG_BYTES) { return THEME_BUNDLE_BAD_CONFIG; }
    if (!source->read(source->context,THEME_CONFIG,0,text,bundle->entry[config_index].size)) {
        return THEME_BUNDLE_IO;
    }
    if (!theme_parse(&parsed,text,bundle->entry[config_index].size)) {
        return THEME_BUNDLE_BAD_CONFIG;
    }
    if (config) { *(Theme *)config=parsed; }
    /* Every image the config names must be in the set, and nothing else may be:
     * this path must not become a way to write arbitrary files. */
    for (i=0; i<2; i++) {
        const char *name=i ? parsed.background : parsed.boot;
        int index;
        if (!name[0]) { continue; }
        index=find(bundle,name);
        if (index<0) { return THEME_BUNDLE_MISSING_IMAGE; }
        used[index]=1;
        status=check_image(bundle,source,name);
        if (status!=THEME_BUNDLE_OK) { return status; }
    }
    for (i=0; i<bundle->count; i++) {
        if (!used[i]) { return THEME_BUNDLE_UNREFERENCED; }
    }
    return THEME_BUNDLE_OK;
}
/* Streams one entry from the source into its final file. */
static ThemeBundleResult commit_file(const ThemeEntry *entry, const BundleSource *source,
                                     const BundleWriter *writer)
{
    u8 body[512];
    char file[13];
    u32 done=0;
    void *handle=0;
    if (!writer->begin || !writer->data || !writer->end) { return THEME_BUNDLE_IO; }
    dotted(entry->name,file);
    if (!writer->begin(writer->context,file,entry->size,&handle)) {
        return THEME_BUNDLE_IO;
    }
    while (done<entry->size) {
        unsigned want=entry->size-done;
        if (want>sizeof(body)) { want=sizeof(body); }
        if (!source->read(source->context,file,done,body,want)) {
            if (writer->abort) { writer->abort(handle); }
            return THEME_BUNDLE_IO;
        }
        if (!writer->data(handle,body,want)) {
            if (writer->abort) { writer->abort(handle); }
            return THEME_BUNDLE_IO;
        }
        done+=want;
    }
    if (!writer->end(handle)) { return THEME_BUNDLE_IO; }
    return THEME_BUNDLE_OK;
}
ThemeBundleResult theme_bundle_commit(const ThemeBundle *bundle, const BundleSource *source,
                                      const BundleWriter *writer)
{
    unsigned i, done_count=0;
    int config_index;
    ThemeBundleResult status;
    if (!bundle || !source || !writer) { return THEME_BUNDLE_MISSING_CONFIG; }
    config_index=find(bundle,THEME_CONFIG);
    if (config_index<0) { return THEME_BUNDLE_MISSING_CONFIG; }
    for (i=0; i<bundle->count; i++) {
        if ((int)i==config_index) { continue; }
        status=commit_file(&bundle->entry[i],source,writer);
        if (status!=THEME_BUNDLE_OK) { return status; }
        done_count++;
    }
    /* The config last, so an interruption leaves a theme that still renders. */
    status=commit_file(&bundle->entry[config_index],source,writer);
    if (status!=THEME_BUNDLE_OK) { return status; }
    (void)done_count;
    return THEME_BUNDLE_OK;
}
