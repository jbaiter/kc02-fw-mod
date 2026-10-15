/* SPDX-License-Identifier: MIT
 * AX3292 USB20 facts: ax329x.h and ax32xx_usb.c / hal_usb_dev_enum.c.
 * This is not the older USB1.1 indirect-register controller.
 */
#ifndef LUMA_AX_USB_H
#define LUMA_AX_USB_H
#include "usb_debug.h"
enum {
    AX_USB_CON0=0x9840, AX_USB_CON1=0x9844, AX_USB_EP0=0x984c,
    AX_USB_TX1=0x9850, AX_USB_RX1=0x9854,
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
typedef struct {
    void *context;
    u32 (*read)(void *, unsigned reg);
    void (*write)(void *, unsigned reg, u32 value);
    u32 (*dma_address)(void *, u8 *);
} AxUsbIo;
typedef struct {
    UsbDebug *usb;
    AxUsbIo io;
    unsigned state, transmitting, halt_in, halt_out;
    u8 ep0[64] __attribute__((aligned(32)));
    u8 rx[64] __attribute__((aligned(32)));
    u8 tx[64] __attribute__((aligned(32)));
} AxUsb;
/* Caller supplies initialized PHY, disabled caches and an exclusive controller.
 * No flash, SD, interrupts, app callbacks, or unbounded waits here. */
void ax_usb_init(AxUsb *, UsbDebug *, const AxUsbIo *);
void ax_usb_poll(AxUsb *);
int ax_usb_idle(const AxUsb *);
#endif
