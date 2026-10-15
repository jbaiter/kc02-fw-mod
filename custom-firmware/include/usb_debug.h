/* SPDX-License-Identifier: MIT
 * USB 1.1 full-speed development debug function. No MSC or flash commands.
 * Control transactions commit address/configuration only after status ACK.
 */
#ifndef LUMA_USB_DEBUG_H
#define LUMA_USB_DEBUG_H
#include "luma.h"
#define USB_DEBUG_PACKET 64
#define USB_DEBUG_QUEUE 4096
#define USB_DEBUG_INTERFACE 0
#define USB_DEBUG_IN 0x81
#define USB_DEBUG_OUT 0x01
#define USB_DEBUG_REVOKE 0x5f /* Vendor OUT, wValue/wIndex/wLength all zero. */

typedef struct {
    u16 vid, pid;
    unsigned address, configured, halt_in, halt_out;
    unsigned pending, value, queued, consumed, fault;
    u8 tx[USB_DEBUG_QUEUE];
    void *context;
    void (*receive)(void *, const u8 *, unsigned);
    void (*revoke)(void *);
} UsbDebug;
void usb_debug_init(UsbDebug *, u16 vid, u16 pid, void *context,
                    void (*receive)(void *, const u8 *, unsigned), void (*revoke)(void *));
void usb_debug_reset(UsbDebug *);
/* -1 stall; otherwise response size (zero means OUT request/status IN). */
int usb_debug_setup(UsbDebug *, const u8 setup[8], u8 response[64]);
/* Effects: bit 0 reconfigure, bit 1 revoke, bit 2 clear IN toggle,
 * bit 3 clear OUT toggle. Clear-toggle applies even if endpoint wasn't halted. */
int usb_debug_status_complete(UsbDebug *);
int usb_debug_send(UsbDebug *, const u8 *, unsigned);
unsigned usb_debug_peek(UsbDebug *, u8 out[64]);
void usb_debug_sent(UsbDebug *, unsigned);
int usb_debug_receive(UsbDebug *, const u8 *, unsigned);
#endif
