/* SPDX-License-Identifier: MIT
 * Peer scaffolding for the WebUSB update path.
 *
 * Separate from the USB device harness on purpose: this covers the descriptors
 * and the staging protocol, and pulling in the controller model as well would
 * make a descriptor test depend on the whole device.
 */
#include <string.h>
#include "webusb.h"

/* The card is supplied by the caller: the WebUSB path needs a real filesystem,
 * and a test can build one and read the result back through the same volume. */
static WebUsb w_usb;
static u8 w_nor[0x400000];
static const char w_app[]="an application that boots";
/* A model NOR with one committed application in slot A, so boot_select finds a
 * bootable record and update_apply can choose the other slot. */
static int w_nor_read(void *ctx, u32 offset, u8 *out, unsigned size)
{
    (void)ctx;
    if (offset>sizeof(w_nor) || size>sizeof(w_nor)-offset) { return 0; }
    memcpy(out,w_nor+offset,size);
    return 1;
}
static int w_nor_erase(void *ctx, u32 offset)
{
    (void)ctx;
    if (offset<0x10000 || (offset%4096) || offset>sizeof(w_nor)-4096) { return 0; }
    memset(w_nor+offset,0xff,4096);
    return 1;
}
static int w_nor_program(void *ctx, u32 offset, const u8 *data, unsigned size)
{
    unsigned i;
    (void)ctx;
    if (offset<0x10000 || !size || offset>sizeof(w_nor)-size) { return 0; }
    for (i=0; i<size; i++) { w_nor[offset+i]&=data[i]; }
    return 1;
}
static const Nor w_nor_iface={0,w_nor_read,w_nor_erase,w_nor_program};
static unsigned w_nor_writes;
static int w_nor_erase_counted(void *ctx, u32 offset)
{
    w_nor_writes++;
    return w_nor_erase(ctx,offset);
}
static int w_nor_program_counted(void *ctx, u32 offset, const u8 *data, unsigned size)
{
    w_nor_writes++;
    return w_nor_program(ctx,offset,data,size);
}
static Nor w_nor_used;
void bringup_h_webusb_init(Fat *volume, u32 board_bit)
{
    WebUsbConfig config;
    unsigned i;
    w_nor_used=w_nor_iface;
    w_nor_used.erase=w_nor_erase_counted;
    w_nor_used.program=w_nor_program_counted;
    w_nor_writes=0;
    memset(w_nor,0xff,sizeof(w_nor));
    for (i=0; w_app[i]; i++) { w_nor[0x20000+i]=(u8)w_app[i]; }
    {
        u8 record[32];
        static const char magic[8]={'L','M','C','O','M','M','I','T'};
        u32 bytes=(u32)(sizeof(w_app)-1);
        u32 payload_crc=~0u;
        u32 record_crc=~0u;
        unsigned j;
        for (j=0; j<bytes; j++) {
            payload_crc^=w_nor[0x20000+j];
            for (unsigned b=0; b<8; b++) { payload_crc=(payload_crc>>1)^(0xedb88320u&(0u-(payload_crc&1))); }
        }
        payload_crc^=0xffffffffu;
        memset(record,0,sizeof(record));
        memcpy(record,magic,8);
        /* Every field is 32-bit little-endian. Writing only the low byte of the
         * CRC leaves a record that looks committed and fails its own check. */
        for (j=0; j<4; j++) {
            record[8+j]=(u8)(1u>>(j*8));                         /* generation 1 */
            record[12+j]=(u8)(0u>>(j*8));                        /* slot A */
            record[16+j]=(u8)(bytes>>(j*8));
            record[20+j]=(u8)(payload_crc>>(j*8));
            record[24+j]=(u8)(1u>>(j*8));                        /* version 1 */
        }
        for (j=0; j<28; j++) {
            record_crc^=record[j];
            for (unsigned b=0; b<8; b++) { record_crc=(record_crc>>1)^(0xedb88320u&(0u-(record_crc&1))); }
        }
        record_crc^=0xffffffffu;
        for (j=0; j<4; j++) { record[28+j]=(u8)(record_crc>>(j*8)); }
        memcpy(w_nor+0x10000,record,sizeof(record));
    }
    config=(WebUsbConfig){volume,&w_nor_used,board_bit,0,0};
    webusb_init(&w_usb,&config);
}
void bringup_h_webusb_confirm(int confirmed) { w_usb.config.locally_confirmed=confirmed; }
void bringup_h_webusb_recovery(int verified) { w_usb.config.recovery_verified=verified; }
void bringup_h_webusb_board(unsigned bit) { w_usb.config.board_bit=bit; }
void bringup_h_webusb_no_writer(void) { w_usb.config.nor=0; }
void bringup_h_webusb_command(unsigned command) { webusb_command(&w_usb,command); }
int bringup_h_webusb_feed(const u8 *bytes, unsigned size) { return webusb_feed(&w_usb,bytes,size); }
unsigned bringup_h_webusb_info(unsigned n)
{
    switch (n) {
    case 0: return w_usb.status.state;
    case 1: return w_usb.status.result;
    case 2: return w_usb.status.kind;
    case 3: return w_usb.status.received;
    case 4: return w_usb.status.total;
    case 5: return w_usb.status.version;
    case 6: return w_usb.status.files;
    case 7: return w_usb.begins;
    case 8: return w_usb.commits;
    case 9: return w_usb.aborts;
    case 10: return w_nor_writes;
    case 11: return 0;
    case 12: return 0;
    case 13: return w_usb.writing;
    case 14: return w_usb.pending_open;
    default: return 0;
    }
}
unsigned bringup_h_webusb_nor(unsigned offset)
{
    return offset<sizeof(w_nor) ? w_nor[offset] : 0xffffffffu;
}
unsigned bringup_h_webusb_status_bytes(u8 *out, unsigned cap)
{
    int n;
    if (!out) { return 0; }
    n=webusb_status_read(&w_usb,out,cap);
    return (unsigned)(n>0 ? n : 0);
}
unsigned bringup_h_webusb_bos(u8 *out, unsigned cap) { return webusb_bos(out,cap); }
unsigned bringup_h_webusb_url_desc(u8 *out, unsigned cap) { return webusb_url(out,cap); }

