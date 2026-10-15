/* SPDX-License-Identifier: MIT
 * AX3292 USB20 device controller: control endpoint plus two bulk endpoints.
 *
 * Register facts come from ax329x.h and ax32xx_usb.c: USB20_Index selects the
 * endpoint whose max-packet and CSR registers are visible, USB20CON1 bits
 * 13+epx carry that endpoint's transfer length, and each endpoint has its own
 * TX/RX address registers at a fixed stride.
 *
 *   endpoint 1   mass storage   registers 0x9850 / 0x9854
 *   endpoint 2   vendor debug   registers 0x9858 / 0x985c
 *
 * The debug endpoint takes its bytes from the device control layer, which
 * already owns that queue. Mass storage takes its bytes from the BOT engine
 * through the rings here, because the engine drives transfers itself.
 *
 * Not modelled, and not claimed: enumeration on silicon, endpoint timing, NAK
 * behaviour, and whether two independent bulk endpoints work at all. The vendor
 * SDK documents EP0 plus EP1..EP4; only EP0 and EP1 are demonstrated by the
 * stock firmware. This is the file that has to meet a real host controller.
 */
#ifndef LUMA_AX_USB_H
#define LUMA_AX_USB_H
#include "usb_device.h"
#define AX_EP_QUEUE 2048
#define AX_USB_ENDPOINTS 3
enum {
    AX_USB_CON0=0x9840, AX_USB_CON1=0x9844, AX_USB_EP0=0x984c,
    AX_USB_ADDRESS=0xa000, AX_USB_POWER=0xa004,
    AX_USB_INTR_TX=0xa008, AX_USB_INTR_RX=0xa010,
    AX_USB_IE_TX=0xa018, AX_USB_IE_RX=0xa020,
    AX_USB_INTR=0xa028, AX_USB_IE=0xa02c, AX_USB_INDEX=0xa038,
    AX_USB_TXMAX_L=0xa040, AX_USB_TXMAX_H=0xa044,
    AX_USB_CSR0=0xa048, AX_USB_TXCSR=0xa048, AX_USB_TXCSR_H=0xa04c,
    AX_USB_RXMAX_L=0xa050, AX_USB_RXMAX_H=0xa054,
    AX_USB_RXCSR=0xa058, AX_USB_RXCSR_H=0xa05c,
    AX_USB_COUNT_L=0xa060, AX_USB_COUNT_H=0xa064
};
/* Address registers, first endpoint at 0x9850 with an eight-byte stride. */
#define AX_USB_EP_TX_REG(n) (0x9850u + ((n)-1u)*8u)
#define AX_USB_EP_RX_REG(n) (0x9854u + ((n)-1u)*8u)
typedef struct {
    void *context;
    u32 (*read)(void *, unsigned reg);
    void (*write)(void *, unsigned reg, u32 value);
    u32 (*dma_address)(void *, u8 *);
} AxUsbIo;
typedef struct {
    unsigned number;         /* Endpoint number, one-based. */
    /* The 64-byte buffers the controller reads from and writes into. */
    u8 packet_tx[USB_DEVICE_PACKET] __attribute__((aligned(32)));
    u8 packet_rx[USB_DEVICE_PACKET] __attribute__((aligned(32)));
    unsigned transmitting;
    unsigned halt_in, halt_out;
    /* Staged bytes for the BOT engine, which drives its own transfers. The
     * debug endpoint leaves these unused and reads the device queue instead. */
    u8 tx[AX_EP_QUEUE], rx[AX_EP_QUEUE];
    unsigned tx_used, tx_sent, rx_used, rx_read;
    unsigned stalled_in, stalled_out;
    unsigned fault;
} AxUsbEndpoint;
typedef struct {
    UsbDevice *usb;
    AxUsbIo io;
    unsigned state, response_size, response_sent;
    u8 ep0[USB_DEVICE_PACKET] __attribute__((aligned(32)));
    AxUsbEndpoint msc;
    AxUsbEndpoint debug;
    AxUsbEndpoint update;
} AxUsb;
/* The caller supplies an initialized PHY, disabled caches and an exclusive
 * controller. No flash, SD, interrupts, app callbacks or unbounded waits. */
void ax_usb_init(AxUsb *, UsbDevice *, const AxUsbIo *);
void ax_usb_poll(AxUsb *);
/* Mass-storage link, for MscLink. */
int ax_usb_msc_send(void *context, const u8 *data, unsigned size);
int ax_usb_msc_receive(void *context, u8 *out, unsigned size);
void ax_usb_msc_stall(void *context, int in);
void ax_usb_msc_reset(void *context);
/* WebUSB uploads arrive on the third endpoint. They are consumed as a byte
 * stream, not as packets, because the container parser decides the framing. */
unsigned ax_usb_update_available(AxUsb *);
int ax_usb_update_receive(AxUsb *, u8 *out, unsigned size);
/* True when at least a whole command block has arrived on the storage
 * endpoint, so the caller knows a complete CBW can be read. */
int ax_usb_msc_cbw_ready(AxUsb *);
#endif
