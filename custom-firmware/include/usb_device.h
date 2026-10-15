/* SPDX-License-Identifier: MIT
 * Composite USB device control layer: descriptors, standard requests, and the
 * class requests the two interfaces need.
 *
 * Two functions share one device:
 *
 *   interface 0   mass storage, class 08/06/50   EP1 IN/OUT   bulk
 *   interface 1   vendor debug, class ff/4c/01   EP2 IN/OUT   bulk
 *
 * A browser cannot drive the mass-storage interface and must not be given the
 * debugger's memory access, which is why they stay separate functions rather
 * than one vendor interface.
 *
 * This layer owns control transfers and the debug endpoint's byte queue. Mass
 * storage data does not pass through here: the BOT engine owns those transfers
 * and they travel through the endpoint layer directly.
 */
#ifndef LUMA_USB_DEVICE_H
#define LUMA_USB_DEVICE_H
#include "luma.h"
#define USB_DEVICE_PACKET 64
#define USB_DEVICE_QUEUE 4096
#define USB_DEVICE_MSC_INTERFACE 0
#define USB_DEVICE_DEBUG_INTERFACE 1
#define USB_DEVICE_MSC_IN 0x81
#define USB_DEVICE_MSC_OUT 0x01
#define USB_DEVICE_DEBUG_IN 0x82
#define USB_DEVICE_DEBUG_OUT 0x02
#define USB_DEVICE_ENDPOINTS 4
#define USB_DEVICE_CONFIG_BYTES 55
#define USB_DEVICE_REVOKE 0x5f /* Vendor OUT, debug interface, all fields zero. */
/* Class requests, as a mass-storage host sends them. */
#define USB_DEVICE_GET_MAX_LUN 0xfe
#define USB_DEVICE_BOT_RESET 0xff
typedef struct {
    u16 vid, pid;
    unsigned address, configured;
    unsigned halt[USB_DEVICE_ENDPOINTS];
    unsigned pending, value, queued, consumed, fault;
    unsigned halt_slot, halt_set;
    u8 tx[USB_DEVICE_QUEUE];
    void *context;
    void (*receive)(void *, const u8 *, unsigned);
    void (*revoke)(void *);
    /* Called after a Bulk-Only Mass Storage Reset completes, so the BOT engine
     * can abort its command and clear its endpoint stalls. */
    void (*bot_reset)(void *context);
    /* Bus reset or disconnect: the host is gone. Distinct from `revoke`, which
     * also fires on reconfiguration while the host is still there. */
    void (*bus_reset)(void *context);
    /* Configuration chosen: the host is ready to be offered the LUN. Named
     * apart from the `configured` state flag, which is what the host asked. */
    void (*on_configured)(void *context);
} UsbDevice;
void usb_device_init(UsbDevice *, u16 vid, u16 pid, void *context,
                     void (*receive)(void *, const u8 *, unsigned), void (*revoke)(void *));
void usb_device_reset(UsbDevice *);
/* -1 stalls; otherwise the response size (zero means an OUT request or a
 * status-only completion). */
int usb_device_setup(UsbDevice *, const u8 setup[8], u8 response[64]);
/* Effects: bit 0 reconfigure, bit 1 revoke, bit 2 clear IN toggle,
 * bit 3 clear OUT toggle. Clear-toggle applies even if the endpoint wasn't
 * halted. */
int usb_device_status_complete(UsbDevice *);
unsigned usb_device_halted(const UsbDevice *, unsigned endpoint);
int usb_device_send(UsbDevice *, const u8 *, unsigned);
unsigned usb_device_peek(UsbDevice *, u8 out[USB_DEVICE_PACKET]);
void usb_device_sent(UsbDevice *, unsigned);
int usb_device_receive(UsbDevice *, const u8 *, unsigned);
#endif
