/* SPDX-License-Identifier: MIT */
#include "ax_usb.h"
enum { IDLE, STATUS_IN, DATA_IN, STATUS_OUT };
static u32 rd(AxUsb *a, unsigned r) { return a->io.read(a->io.context,r); }
static void wr(AxUsb *a, unsigned r, u32 v) { a->io.write(a->io.context,r,v); }
static void address(AxUsb *a, unsigned r, u8 *p)
{
    wr(a,r,a->io.dma_address(a->io.context,p));
}
static void endpoints(AxUsb *a)
{
    wr(a,AX_USB_INDEX,1);
    wr(a,AX_USB_TXMAX_L,64); wr(a,AX_USB_TXMAX_H,0);
    wr(a,AX_USB_RXMAX_L,64); wr(a,AX_USB_RXMAX_H,0);
    wr(a,AX_USB_TXCSR_H,0x20); /* Peripheral bulk transmit mode. */
    wr(a,AX_USB_RXCSR_H,0);
    wr(a,AX_USB_TXCSR,0x48); /* Flush TX FIFO, clear data toggle. */
    wr(a,AX_USB_RXCSR,0x90); /* Flush RX FIFO, clear data toggle. */
    address(a,AX_USB_TX1,a->tx); address(a,AX_USB_RX1,a->rx);
    a->transmitting=a->halt_in=a->halt_out=0;
    wr(a,AX_USB_INDEX,0); address(a,AX_USB_EP0,a->ep0);
}
static void reset(AxUsb *a)
{
    usb_debug_reset(a->usb); a->state=IDLE;
    wr(a,AX_USB_ADDRESS,0); endpoints(a);
}
void ax_usb_init(AxUsb *a, UsbDebug *u, const AxUsbIo *io)
{
    *a=(AxUsb){0}; a->usb=u; a->io=*io;
    wr(a,AX_USB_POWER,0); /* Remain detached during configuration. */
    wr(a,AX_USB_IE_TX,0); wr(a,AX_USB_IE_RX,0); wr(a,AX_USB_IE,0);
    reset(a);
    (void)rd(a,AX_USB_INTR); (void)rd(a,AX_USB_INTR_TX); (void)rd(a,AX_USB_INTR_RX);
    /* Full speed only. No HS enable or incorrect 64-byte HS bulk descriptor. */
    if (u->vid && u->pid) { wr(a,AX_USB_POWER,0x40); }
}
static void control(AxUsb *a, unsigned tx_irq)
{
    unsigned csr, count, effects;
    u8 s[8], response[64];
    int n;
    wr(a,AX_USB_INDEX,0); csr=rd(a,AX_USB_CSR0);
    if (csr&0x10) { /* SETUPEND aborts old pending address/configuration. */
        wr(a,AX_USB_CSR0,0x80); a->usb->pending=0; a->state=IDLE;
        csr=rd(a,AX_USB_CSR0);
    }
    if (csr&4) { wr(a,AX_USB_CSR0,0); a->usb->pending=0; a->state=IDLE; }
    if (a->state==STATUS_IN && tx_irq) {
        effects=(unsigned)usb_debug_status_complete(a->usb);
        wr(a,AX_USB_ADDRESS,a->usb->address);
        if (effects&1) { endpoints(a); } /* SET_CONFIGURATION resets toggles. */
        else if (effects&2) {
            wr(a,AX_USB_INDEX,1); wr(a,AX_USB_TXCSR,8); wr(a,AX_USB_RXCSR,0x10);
            a->transmitting=0; wr(a,AX_USB_INDEX,0); /* Keep endpoint toggles. */
        }
        if (effects&12) {
            wr(a,AX_USB_INDEX,1);
            if (effects&4) { wr(a,AX_USB_TXCSR,0x48); a->transmitting=0; a->halt_in=0; }
            if (effects&8) { wr(a,AX_USB_RXCSR,0x90); a->halt_out=0; }
            wr(a,AX_USB_INDEX,0);
        }
        a->state=IDLE;
    }
    if (a->state==DATA_IN && tx_irq && !(csr&2)) { a->state=STATUS_OUT; }
    if (!(csr&1)) { return; } /* AX3292 CSR0 RX ready is bit 0, TX is bit 1. */
    count=rd(a,AX_USB_COUNT_L);
    if (a->state==STATUS_OUT && !count) {
        wr(a,AX_USB_CSR0,0x40); a->state=IDLE; return;
    }
    if (count!=8 || a->state!=IDLE) {
        wr(a,AX_USB_CSR0,0x60); a->usb->pending=0; a->state=IDLE; return;
    }
    for (count=0; count<8; count++) { s[count]=a->ep0[count]; }
    n=usb_debug_setup(a->usb,s,response);
    if (n<0) { wr(a,AX_USB_CSR0,0x60); return; } /* service RX + send stall */
    if (!(s[0]&0x80)) {
        a->state=STATUS_IN;
        wr(a,AX_USB_CSR0,0x48); /* Service RX + DATAEND: automatic IN status. */
        return;
    }
    wr(a,AX_USB_CSR0,0x40);
    for (count=0; count<(unsigned)n; count++) { a->ep0[count]=response[count]; }
    address(a,AX_USB_EP0,a->ep0);
    wr(a,AX_USB_CON1,(1u<<13)|(unsigned)n);
    a->state=DATA_IN; wr(a,AX_USB_CSR0,0x0a); /* TX ready + DATAEND. */
}
void ax_usb_poll(AxUsb *a)
{
    unsigned intr=rd(a,AX_USB_INTR), tx=rd(a,AX_USB_INTR_TX), csr, n;
    (void)rd(a,AX_USB_INTR_RX);
    if (intr&(4|0x20)) { reset(a); return; } /* Bus reset / disconnect. */
    if (intr&1) {
        /* Suspend is not a bus reset: retain address/configuration/toggles. */
        if (a->usb->revoke) { a->usb->revoke(a->usb->context); }
        a->usb->queued=a->usb->consumed=0;
        wr(a,AX_USB_INDEX,1); wr(a,AX_USB_TXCSR,8); wr(a,AX_USB_RXCSR,0x10);
        a->transmitting=0; return;
    }
    control(a,tx&1);
    if (!a->usb->configured) { return; }
    wr(a,AX_USB_INDEX,1);
    if (a->halt_in!=a->usb->halt_in) {
        wr(a,AX_USB_TXCSR,a->usb->halt_in ? 0x10 : 0x48);
        a->halt_in=a->usb->halt_in;
        a->transmitting=0; /* An unacknowledged flushed packet stays queued. */
    }
    if (a->halt_out!=a->usb->halt_out) {
        wr(a,AX_USB_RXCSR,a->usb->halt_out ? 0x20 : 0x90);
        a->halt_out=a->usb->halt_out;
    }
    csr=rd(a,AX_USB_TXCSR);
    if (a->transmitting && !(csr&1)) {
        usb_debug_sent(a->usb,a->transmitting); a->transmitting=0;
    }
    csr=rd(a,AX_USB_RXCSR);
    if ((csr&1) && !a->usb->halt_out) {
        n=rd(a,AX_USB_COUNT_L)|(rd(a,AX_USB_COUNT_H)<<8);
        if (n>64 || !usb_debug_receive(a->usb,a->rx,n)) {
            a->usb->fault=1;
            if (a->usb->revoke) { a->usb->revoke(a->usb->context); }
        }
        address(a,AX_USB_RX1,a->rx); wr(a,AX_USB_RXCSR,0);
    }
    if (!a->transmitting && !(rd(a,AX_USB_TXCSR)&1)) {
        n=usb_debug_peek(a->usb,a->tx);
        if (n) {
            address(a,AX_USB_TX1,a->tx); wr(a,AX_USB_CON1,(1u<<14)|n);
            a->transmitting=n; wr(a,AX_USB_TXCSR,1);
        }
    }
}
int ax_usb_idle(const AxUsb *a)
{
    return !a->transmitting && a->usb->queued==a->usb->consumed;
}
