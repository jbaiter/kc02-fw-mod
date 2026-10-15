/* SPDX-License-Identifier: MIT
 * Theme bundle validation and commit, for a theme delivered over USB.
 *
 * A theme is not one file: it is a config plus the images that config names. So
 * a bundle is validated as a set, and committed in an order that leaves a
 * rendering theme at every interruption point:
 *
 *   images first, config last
 *
 * Stopping between the two leaves the new images and the old config. The old
 * config names the images it always named, so the theme still renders. The
 * reverse order would leave a config naming an image that is not there yet.
 *
 * A config may only reference files that are in the bundle. That is what stops
 * this path from becoming a way to write arbitrary files to someone's card.
 *
 * Both sides are streaming. Nothing here ever holds a whole image: the source
 * hands out byte ranges, and the writer takes bytes in order. A 230 KB BMP
 * therefore costs 512 bytes of scratch plus the CRC state.
 */
#ifndef LUMA_THEME_BUNDLE_H
#define LUMA_THEME_BUNDLE_H
#include "luma.h"
#define THEME_BUNDLE_MAX 4
#define THEME_BUNDLE_BYTES (512u*1024u)
#define THEME_BUNDLE_FILE_MAX (256u*1024u)
#define THEME_CONFIG_BYTES 4096u
#define THEME_CONFIG "THEME.INI"
typedef struct {
    char name[13];      /* Uppercase 8.3, as it will appear on the card. */
    u32 size;
    u32 crc;
} ThemeEntry;
typedef struct {
    unsigned count;
    u32 bytes;
    ThemeEntry entry[THEME_BUNDLE_MAX];
} ThemeBundle;
/* Hands out a byte range of a staged file. Both callbacks are given the
 * uppercase dotted 8.3 name ("BOOT.BMP"), which is what the config names and
 * what the FAT layer takes; the internal space padding stays internal. */
typedef struct {
    void *context;
    int (*read)(void *context, const char *name, u32 offset, u8 *out, unsigned size);
} BundleSource;
/* Opens a final file, takes its bytes in order, then publishes or abandons it. */
typedef struct {
    void *context;
    int (*begin)(void *context, const char *name, u32 size, void **handle);
    int (*data)(void *handle, const u8 *bytes, unsigned size);
    int (*end)(void *handle);
    void (*abort)(void *handle);
} BundleWriter;
typedef enum {
    THEME_BUNDLE_OK = 0,
    THEME_BUNDLE_MISSING_CONFIG,   /* No THEME.INI in the set. */
    THEME_BUNDLE_BAD_CONFIG,       /* The config did not parse. */
    THEME_BUNDLE_MISSING_IMAGE,    /* The config names an image not supplied. */
    THEME_BUNDLE_BAD_IMAGE,        /* An image is not a 320x240 24-bit BMP. */
    THEME_BUNDLE_UNREFERENCED,     /* A file the config does not use. */
    THEME_BUNDLE_TOO_BIG,          /* Over the bundle or file limit. */
    THEME_BUNDLE_IO,               /* A read or write failed. */
    THEME_BUNDLE_BAD_NAME,         /* Not a usable 8.3 name. */
} ThemeBundleResult;
/* Adds a file to the set, replacing the entry if the name repeats. */
int theme_bundle_add(ThemeBundle *bundle, const char *name, u32 size, u32 crc);
/* Reads the config and every image it names and checks the whole set. Reads
 * only: nothing on the card changes. `config` receives the parsed theme so a
 * caller can show what is about to be applied. */
ThemeBundleResult theme_bundle_check(const ThemeBundle *bundle, const BundleSource *source,
                                     void *config);
/* Writes every entry to its final name, images first and the config last. */
ThemeBundleResult theme_bundle_commit(const ThemeBundle *bundle, const BundleSource *source,
                                      const BundleWriter *writer);
#endif
