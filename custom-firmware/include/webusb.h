/* SPDX-License-Identifier: MIT
 * WebUSB: the descriptors a browser reads, and the staging protocol it uses to
 * send a firmware or theme update.
 *
 * Descriptors follow the WebUSB specification (https://wicg.github.io/webusb/)
 * and Chrome's implementation notes:
 *
 *   BOS descriptor            bLength 5, type 0x0F
 *   Platform capability       bLength 24, type 0x10, type 0x05,
 *                             PlatformCapabilityUUID = the WebUSB UUID in
 *                             little-endian byte order,
 *                             bcdVersion 0x0100, bVendorCode, iLandingPage
 *   GET_URL                   bmRequestType 0xC0, bRequest = bVendorCode,
 *                             wValue = iLandingPage, wIndex = 2,
 *                             data = URL descriptor in UTF-8
 *
 * A BOS needs bcdUSB 0x0210 or later, so the device descriptor had to change
 * with it. That is not cosmetic: a 1.1 device cannot have a BOS at all, and a
 * browser would never see the capability.
 *
 * The landing page is a URL and this project does not host one yet. With no URL
 * configured the descriptor reports iLandingPage 0, which means "no landing
 * page": the mechanism is implemented and tested, and only the page is missing.
 * Claiming a URL that does not exist would be worse than reporting none.
 *
 * The update itself: a small container arrives on the update endpoint, is
 * written to the card as one staging file while its CRCs are checked, and is
 * only applied on a separate command. Applying firmware still needs a
 * device-local confirmation that USB cannot supply.
 */
#ifndef LUMA_WEBUSB_H
#define LUMA_WEBUSB_H
#include "luma.h"
#include "storage.h"
#include "update.h"
#include "theme_bundle.h"
/* Vendor request code the platform capability advertises. */
#define WEBUSB_VENDOR_CODE 0x01
#define WEBUSB_GET_URL 2
#define WEBUSB_DESCRIPTOR_TYPE_URL 0x03
#define WEBUSB_URL_SCHEME_HTTP 0x00
#define WEBUSB_URL_SCHEME_HTTPS 0x01
#ifndef WEBUSB_URL
#define WEBUSB_URL ""      /* No hosted landing page yet: see the header note. */
#endif
/* Vendor requests on the update interface. Control carries the command only;
 * the container arrives on the update endpoint. */
enum {
    WEBUSB_CMD_BEGIN = 0x01,
    WEBUSB_CMD_COMMIT = 0x02,
    WEBUSB_CMD_ABORT = 0x03,
    WEBUSB_CMD_STATUS = 0x81,
};
/* Staged container, little-endian, sent on the update endpoint.
 *
 *   header (24)  magic[8] "LUMAUPD\0", u16 format, u16 header_bytes,
 *                u16 kind, u16 count, u32 total_bytes, u32 payload crc32
 *   entry (20)   name[11] space-padded 8.3, u8 pad, u32 length, u32 crc32
 *   payload      the file bytes, concatenated in entry order
 *
 * The entry is 20 bytes and not 16: an 11-byte name plus a pad plus two u32
 * fields. A 16-byte entry puts the CRC over the last three bytes of the name,
 * which corrupts every extension.
 */
#define WEBUSB_MAGIC "LUMAUPD"
#define WEBUSB_FORMAT 1
#define WEBUSB_HEADER_BYTES 24
#define WEBUSB_ENTRY_BYTES 20
#define WEBUSB_FILES_MAX 4
#define WEBUSB_FIRMWARE 1
#define WEBUSB_THEME 2
#define WEBUSB_STAGE_NAME "UPDATE.NEW"
#define WEBUSB_STATUS_BYTES 32
/* Result codes: the staged path has its own, then the engine codes follow in
 * their own ranges so a caller can tell which layer refused. */
enum {
    WEBUSB_RESULT_OK = 0,
    WEBUSB_RESULT_BAD_CRC = 1,
    WEBUSB_RESULT_MEDIUM = 2,
    WEBUSB_RESULT_BAD_IMAGE = 3,
    WEBUSB_RESULT_NO_WRITER = 4,
    WEBUSB_RESULT_NOT_CONFIRMED = 5,
    WEBUSB_RESULT_NOT_STAGED = 6,
    WEBUSB_RESULT_THEME_BASE = 16,   /* + ThemeBundleResult */
    WEBUSB_RESULT_ENGINE_BASE = 32,  /* + UpdateResult */
};
typedef enum {
    WEBUSB_IDLE = 0,
    WEBUSB_STAGING,      /* A container is arriving. */
    WEBUSB_STAGED,       /* Written and CRC-checked, not applied. */
    WEBUSB_APPLIED,      /* Applied. */
    WEBUSB_FAILED,       /* The container or the medium was bad. */
    WEBUSB_DENIED,       /* Refused: see the result code. */
} WebUsbState;
typedef struct {
    Fat *volume;
    const Nor *nor;          /* NULL until a NOR driver exists. */
    u32 board_bit;
    /* Only a device-local action may set this: a browser cannot confirm its own
     * update, which is the whole point of the gate. */
    int locally_confirmed;
    int recovery_verified;
} WebUsbConfig;
typedef struct {
    u32 magic;
    u32 state;
    u32 result;
    u32 kind;
    u32 received;
    u32 total;
    u32 version;
    u32 files;
} WebUsbStatus;
typedef struct {
    WebUsbConfig config;
    WebUsbStatus status;
    u8 header[WEBUSB_HEADER_BYTES+WEBUSB_ENTRY_BYTES*WEBUSB_FILES_MAX];
    unsigned header_have, header_need;
    FatWrite writer;
    int writing;
    u32 remaining;           /* Container bytes still to arrive. */
    u32 crc;                 /* Running CRC over the file bytes. */
    u32 data_offset;         /* Where the file bytes begin in the container. */
    ThemeBundle bundle;
    FileReader stage;
    unsigned stage_open;
    /* One pending theme file at a time, so a commit never needs two writers. */
    FatWrite pending;
    unsigned pending_open;
    u32 crc_expected, crc_actual;
    u32 entry_offset[WEBUSB_FILES_MAX];
    u32 entry_length[WEBUSB_FILES_MAX];
    char dotted[WEBUSB_FILES_MAX][13];
    u8 status_bytes[WEBUSB_STATUS_BYTES];
    /* Counters, read back by tests. */
    unsigned begins, commits, aborts;
} WebUsb;
void webusb_init(WebUsb *w, const WebUsbConfig *config);
/* Control layer hooks. */
void webusb_command(WebUsb *w, unsigned command);   /* applies BEGIN/COMMIT/ABORT */
/* Serialises the status into status_bytes for webusb_status_read. */
void webusb_status_sync(WebUsb *w);
int webusb_status_read(WebUsb *w, u8 *out, unsigned size);
/* Bytes from the update endpoint. Returns 0 when the container was rejected. */
int webusb_feed(WebUsb *w, const u8 *bytes, unsigned size);
/* The descriptors a browser reads. */
unsigned webusb_bos(u8 *out, unsigned cap);
unsigned webusb_url(u8 *out, unsigned cap);
#endif
